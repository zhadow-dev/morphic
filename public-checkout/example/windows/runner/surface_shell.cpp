#include "surface_shell.h"

#include <dwmapi.h>
#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <algorithm>
#include <cstdio>

#include "flutter/generated_plugin_registrant.h"
#include "forensic_trace.h"
#include "interaction_router.h"
#include "process_lifecycle.h"
#include "qualification_probe.h"
#include "surface_manager.h"
#include "surface_model.h"

namespace {
// PHASE 7E — diagnostic flag for the resize-flicker investigation. Flip to
// `true` to flood the trace with per-message timing for WM_SIZE /
// WM_ERASEBKGND / WM_PAINT / ApplyGeometry. Off by default so production
// logs stay readable.
constexpr bool kLogRepaintTiming = false;

// PHASE 7E-Reg — interaction forensics for the post-refactor regression check.
// DEFAULT ON so the user can see exactly where the interaction chain breaks
// (hit-test → ncbuttondown → router → setcapture → mousemove → setbounds →
// applygeometry → native rect). Flip off once interaction is confirmed stable.
constexpr bool kInteractionForensics = true;

// PHASE 11E-B.2R — DWM material qualification probe (throwaway experiment; flip to false to
// revert). History: 2P (1px NCCALCSIZE) and 2Q (WS_THICKFRAME + NCCALCSIZE→0) were BOTH
// insufficient → materials stayed fallback-grey. So the suspect is now REAL non-client frame
// SURVIVAL. When true: the shell adds WS_THICKFRAME at CreateWindowEx AND lets WM_NCCALCSIZE
// fall through to DefWindowProc (the real sizing-border frame survives — the frame DWM
// renders the material into), while custom WM_NCHITTEST + WM_NCLBUTTONDOWN interception keep
// runtime-owned drag/resize (NO native SC_MOVE/SC_SIZE). RISK: first probe that lets the OS
// keep a non-client area — watch interaction + a thin native border.
//
// RESULT: 2R ALSO failed — even a real surviving WS_THICKFRAME frame left the material as
// fallback-grey (it only added a visible native border). So WS_THICKFRAME + frame survival
// is NOT the SYSTEMBACKDROP qualifier. Reverted to false (clean frameless). Remaining
// documented path (WS_CAPTION / WS_OVERLAPPEDWINDOW) reintroduces native caption → the
// SC_MOVE/SC_SIZE modal loops 7A exists to kill; lower-risk alternative is the legacy
// SetWindowCompositionAttribute / ACCENT acrylic path (no style change). See discussion.
constexpr bool kDwmFrameProbe = false;
}  // namespace

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_DONOTROUND
#define DWMWCP_DONOTROUND 1
#endif

namespace {
constexpr const wchar_t kWindowClassName[] = L"MORPHIC_SURFACE_SHELL";
constexpr int kTitleStrip = 30;  // top drag zone (custom "caption")
constexpr int kBorder = 6;       // resize border on the other edges
HBRUSH g_frame_brush = nullptr;
}  // namespace

SurfaceShell::SurfaceShell(SurfaceManager* manager, std::string id)
    : manager_(manager), id_(std::move(id)) {}

void SurfaceShell::ReleaseEngine() { controller_.reset(); }

namespace {
// ENGINE RETENTION — PHASE R0. At graceful app exit, LEAK the engine
// (unique_ptr::release → no FlutterViewController destructor → no
// flutter_windows.dll teardown race) and let process termination reclaim it.
// During the session this is reset() as before. Returns whether it leaked.
bool ReleaseOrLeakController(
    std::unique_ptr<flutter::FlutterViewController>& controller,
    const std::string& id) {
  if (!controller) return false;
  if (morphic::ProcessExiting().load()) {
    (void)controller.release();  // intentional leak — OS reclaims at exit
    forensic::Log("ENGINE", "exit: leaked engine id=" + id +
                                " (no reset; OS reclaims) [R0]");
    return true;
  }
  controller.reset();
  return false;
}
}  // namespace

SurfaceShell::~SurfaceShell() {
  // Tear down Flutter before the window it renders into — UNLESS we're exiting,
  // in which case leak it (R0: process termination reclaims; reset() would only
  // pay the teardown-race crash for shutdown purity).
  ReleaseOrLeakController(controller_, id_);
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

void SurfaceShell::EnsureWindowClass() {
  static bool registered = false;
  if (registered) {
    return;
  }
  if (!g_frame_brush) {
    g_frame_brush = CreateSolidBrush(RGB(40, 42, 54));  // surface frame color
  }
  WNDCLASS wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = GetModuleHandle(nullptr);
  wc.lpszClassName = kWindowClassName;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  // PHASE 7E — CS_HREDRAW|CS_VREDRAW invalidate the ENTIRE client on every
  // resize. With the runtime-paced resize loop that fires ~60 invalidations/sec
  // per drag, each one followed by a default-brush erase that flashes the
  // dark frame color before Flutter catches up. Drop both: we'll selectively
  // invalidate the chrome regions and rely on Flutter to repaint its own area.
  wc.style = 0;
  // PHASE 7E — hbrBackground = nullptr means DefWindowProc returns 0 from
  // WM_ERASEBKGND (claims no erase happened). We then handle WM_ERASEBKGND
  // ourselves and paint ONLY the title strip + resize borders — never the
  // child's interior — so the frame color never bleeds into the area Flutter
  // is about to repaint.
  wc.hbrBackground = nullptr;
  RegisterClass(&wc);
  registered = true;
}

bool SurfaceShell::Create(const flutter::DartProject& base_project,
                          const std::string& entrypoint, int x, int y,
                          int width, int height, DWORD ex_style, HWND owner,
                          bool chromeless) {
  EnsureWindowClass();

  // SPATIAL CHROME — a plain value handed down by the policy layer (same firewall
  // pattern as ex_style/owner). Chromeless zeroes the strip + borders so the
  // Flutter child fills the window and no native chrome is ever painted.
  chromeless_ = chromeless;
  strip_px_ = chromeless ? 0 : kTitleStrip;
  border_px_ = chromeless ? 0 : kBorder;

  // Frameless application window (PHASE 2.5): WS_POPUP (no caption/frame/sysmenu)
  // keeps the compositor look. The extended style is now CALLER-CHOSEN (PHASE
  // 10.3): WS_EX_APPWINDOW for task-switchable workspace surfaces, WS_EX_TOOLWINDOW
  // for utilities/overlays/launcher (removes them from Alt+Tab + taskbar),
  // optionally WS_EX_TOPMOST for overlays. The shell does NOT know which kind it is
  // — it applies the flags it's handed (sacred firewall). DWM frame/shadow stays
  // off via WM_NCCALCSIZE→0 + no WS_CAPTION/WS_THICKFRAME regardless of ex-style.
  // `owner` (8th CreateWindowEx arg) is the Win32 owner HWND for owned surfaces
  // (palettes/inspectors/overlays owned by their workspace), null for top-level.
  // PHASE 11E-B.2Q — base frameless style; the probe adds WS_THICKFRAME (a real DWM frame
  // for SYSTEMBACKDROP material eligibility) WITHOUT changing interaction: custom
  // WM_NCHITTEST + WM_NCLBUTTONDOWN interception still own drag/resize, and WM_NCCALCSIZE→0
  // keeps it visually frameless. Default-off; isolates the window-STYLE variable.
  DWORD style = WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  if constexpr (morphic::probe::kQualificationProbe) {
    // 11G-PROBE — full overlapped qualification: a REAL caption + sizing frame (the
    // documented SYSTEMBACKDROP "main window" qualifier). Supersedes the WS_THICKFRAME-only
    // 2R probe. Native caption WILL be visible (ugly-but-truthful). Interaction stays
    // runtime-owned via WM_SYSCOMMAND (eats SC_MOVE/SC_SIZE) + WM_NCLBUTTONDOWN interception.
    style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
  } else if constexpr (kDwmFrameProbe) {
    style |= WS_THICKFRAME;
  }
  hwnd_ = CreateWindowEx(ex_style, kWindowClassName, L"Morphic", style, x, y, width,
                         height, owner, nullptr, GetModuleHandle(nullptr), this);
  if (!hwnd_) {
    return false;
  }
  // PHASE 10.3 — semantic/native behavior trace (one line per surface).
  {
    char buf[160];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "id=%s exstyle=0x%08lX owner=0x%p%s%s", id_.c_str(),
                static_cast<unsigned long>(ex_style),
                static_cast<void*>(owner),
                (ex_style & WS_EX_TOOLWINDOW) ? " TOOLWINDOW" : " APPWINDOW",
                (ex_style & WS_EX_TOPMOST) ? " TOPMOST" : "");
    forensic::Log("SHELL", buf);
  }
  ConfigureDwm();
  forensic::DumpWindowStyles("SURFACE", hwnd_,
                             ("CreateWindowEx done id=" + id_ + ":").c_str());

  // PHASE 7E — confirm clipping flags survived CreateWindowEx. Missing
  // CLIPCHILDREN is the textbook cause of parent-overdraw flicker (the parent
  // paints the child's region with the frame brush every WM_PAINT, then the
  // child overpaints it later). Verified at creation rather than trusting the
  // literal in the call below — defensive, cheap.
  {
    DWORD actual_style = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));
    if (!(actual_style & WS_CLIPCHILDREN)) {
      forensic::Log("REPAINT WARN",
                    "WS_CLIPCHILDREN MISSING on id=" + id_ +
                        " — parent overdraw flicker expected");
    }
    if (!(actual_style & WS_CLIPSIBLINGS)) {
      forensic::Log("REPAINT WARN",
                    "WS_CLIPSIBLINGS MISSING on id=" + id_);
    }
  }

  RECT rc{};
  GetClientRect(hwnd_, &rc);
  int content_w = (rc.right - rc.left) - 2 * border_px_;
  int content_h = (rc.bottom - rc.top) - strip_px_ - border_px_;

  // One engine per surface, running the surface's Dart entrypoint.
  flutter::DartProject project = base_project;
  project.set_dart_entrypoint(entrypoint);
  controller_ = std::make_unique<flutter::FlutterViewController>(
      content_w > 0 ? content_w : 1, content_h > 0 ? content_h : 1, project);
  if (!controller_->engine() || !controller_->view()) {
    forensic::Log("SURFACE", "FlutterViewController invalid id=" + id_);
    return false;
  }

  RegisterPlugins(controller_->engine());

  child_ = controller_->view()->GetNativeWindow();
  SetParent(child_, hwnd_);
  LayoutChild();
  ShowWindow(child_, SW_SHOW);

  controller_->engine()->SetNextFrameCallback([this]() {
    forensic::Log("FRAME",
                  "first frame id=" + id_ + " -> Show + participation/DWM dump");
    Show();
    forensic::DumpHwndGraph(hwnd_);
    forensic::DumpDwmState(hwnd_);
    LogParticipation();
    // PHASE 7E-Reg — run the interaction self-check AFTER Show so the surface
    // is in its final visible geometry. This is the single most important
    // signal in the trace: any FAIL line means runtime-owned interaction is
    // structurally broken and won't work no matter how the rest of the chain
    // is wired.
    RunInteractionSelfCheck();
    // M2.1D — engine is up + Dart is running: announce readiness so the lifecycle relay can
    // hand this surface its own opaque id (and downstream app coordination can begin).
    if (manager_) manager_->OnSurfaceReady(this);
  });
  controller_->ForceRedraw();
  return true;
}

void SurfaceShell::Show() {
  // Visibility is a semantic authority owned by SurfaceModel; route through it
  // (it projects to ShowWindow). Fall back to a direct show if unwired.
  if (manager_ && manager_->model()) {
    manager_->model()->Show(this);
  } else {
    ShowWindow(hwnd_, SW_SHOWNA);
  }
}

void SurfaceShell::RunInteractionSelfCheck() {
  // CHECK 5 (one-shot at first frame) — verify the structural invariants
  // that runtime-owned interaction depends on. Anything FAIL here is a
  // structural regression that must be fixed BEFORE chasing symptomatic bugs.
  forensic::Log("INTERACTION SELFCHECK", "BEGIN id=" + id_);

  // 1. Clipping flags (sanity — should already be checked at Create).
  DWORD style = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));
  const bool clip_kids = (style & WS_CLIPCHILDREN) != 0;
  const bool clip_sibs = (style & WS_CLIPSIBLINGS) != 0;
  forensic::Log("INTERACTION SELFCHECK",
                "WS_CLIPCHILDREN=" + std::string(clip_kids ? "1" : "0") +
                    " WS_CLIPSIBLINGS=" + std::string(clip_sibs ? "1" : "0"));
  if (!clip_kids) {
    forensic::Log("INTERACTION SELFCHECK FAIL",
                  "WS_CLIPCHILDREN missing — parent will overdraw child region");
  }

  // 2. Shell + child screen rects.
  RECT shell_rect{};
  GetWindowRect(hwnd_, &shell_rect);
  RECT child_rect{};
  if (child_) GetWindowRect(child_, &child_rect);

  forensic::Log("INTERACTION SELFCHECK",
                "shell_screen=(" + std::to_string(shell_rect.left) + "," +
                    std::to_string(shell_rect.top) + " " +
                    std::to_string(shell_rect.right - shell_rect.left) + "x" +
                    std::to_string(shell_rect.bottom - shell_rect.top) + ")");
  if (child_) {
    forensic::Log("INTERACTION SELFCHECK",
                  "child_screen=(" + std::to_string(child_rect.left) + "," +
                      std::to_string(child_rect.top) + " " +
                      std::to_string(child_rect.right - child_rect.left) +
                      "x" +
                      std::to_string(child_rect.bottom - child_rect.top) + ")");

    // 3. Child must NOT overlap the title strip. If it does, WM_NCHITTEST
    //    on strip clicks will hit the child window instead of us, and the
    //    runtime never sees the drag intent. (A chromeless surface has no
    //    strip — strip_px_ is 0 and the full-bleed child is correct.)
    const LONG strip_bottom_screen = shell_rect.top + strip_px_;
    if (child_rect.top < strip_bottom_screen) {
      forensic::Log("INTERACTION SELFCHECK FAIL",
                    "child overlaps title strip! child.top=" +
                        std::to_string(child_rect.top) + " strip_bottom=" +
                        std::to_string(strip_bottom_screen) +
                        " — drag will not work because clicks hit Flutter "
                        "child instead of HTCAPTION");
    } else {
      forensic::Log("INTERACTION SELFCHECK",
                    "child top edge OK (below title strip by " +
                        std::to_string(child_rect.top - strip_bottom_screen) +
                        "px)");
    }

    // 4. Child must not overlap left/right/bottom resize borders either.
    if (child_rect.left < shell_rect.left + border_px_ ||
        child_rect.right > shell_rect.right - border_px_ ||
        child_rect.bottom > shell_rect.bottom - border_px_) {
      forensic::Log("INTERACTION SELFCHECK FAIL",
                    "child encroaches resize borders — resize will not work");
    }
  } else {
    forensic::Log("INTERACTION SELFCHECK",
                  "child HWND null (Flutter view not yet attached)");
  }

  // 5. Synthetic HitTest probe. lparam for WM_NCHITTEST is SCREEN coords
  //    packed as MAKELPARAM(x, y). Probe a point in the title strip and a
  //    point in the center of the shell — they MUST return HTCAPTION and
  //    HTCLIENT respectively.
  auto probe = [this, &shell_rect](int rel_x, int rel_y, const char* label,
                                    int expected) {
    POINT p{shell_rect.left + rel_x, shell_rect.top + rel_y};
    LPARAM lp = MAKELPARAM(p.x, p.y);
    LRESULT got = HitTest(lp);
    const bool ok = got == expected;
    forensic::Log(ok ? "INTERACTION SELFCHECK" : "INTERACTION SELFCHECK FAIL",
                  std::string("HitTest ") + label + " (rel " +
                      std::to_string(rel_x) + "," + std::to_string(rel_y) +
                      ") = " + std::to_string(got) + " (expected " +
                      std::to_string(expected) + ")");
  };
  // SPATIAL CHROME — a chromeless surface has no strip/edge zones; probing
  // them would expect HTCAPTION/HTLEFT where HTCLIENT is correct.
  if (!chromeless_) {
    probe(border_px_ + 50, strip_px_ / 2, "title-strip", HTCAPTION);
    probe(2, strip_px_ + 50, "left-edge", HTLEFT);
  }
  probe(static_cast<int>(shell_rect.right - shell_rect.left) / 2,
        static_cast<int>(shell_rect.bottom - shell_rect.top) / 2,
        "center", HTCLIENT);

  forensic::Log("INTERACTION SELFCHECK", "END id=" + id_);
}

void SurfaceShell::LogParticipation() const {
  DWORD style = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_STYLE));
  DWORD ex = static_cast<DWORD>(GetWindowLongPtr(hwnd_, GWL_EXSTYLE));
  const bool toolwindow = (ex & WS_EX_TOOLWINDOW) != 0;
  const bool appwindow = (ex & WS_EX_APPWINDOW) != 0;
  HWND owner = GetWindow(hwnd_, GW_OWNER);
  const bool visible = IsWindowVisible(hwnd_) != 0;
  // Documented shell rule: a top-level window is a taskbar/Alt-Tab citizen iff
  // it is visible, unowned, and (WS_EX_APPWINDOW || !WS_EX_TOOLWINDOW).
  const bool eligible = visible && owner == nullptr && (appwindow || !toolwindow);

  char buf[256];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "participation id=%s visible=%d owner=0x%p STYLE=0x%08lX "
              "TOOLWINDOW=%d APPWINDOW=%d -> taskbar/AltTab_eligible=%d",
              id_.c_str(), visible ? 1 : 0, static_cast<void*>(owner),
              static_cast<unsigned long>(style), toolwindow ? 1 : 0,
              appwindow ? 1 : 0, eligible ? 1 : 0);
  forensic::Log("SHELL", buf);
}

void SurfaceShell::ConfigureDwm() {
  DWORD corner = DWMWCP_DONOTROUND;  // sharp edges (override Win11 rounding)
  DwmSetWindowAttribute(hwnd_, DWMWA_WINDOW_CORNER_PREFERENCE, &corner,
                        sizeof(corner));
  BOOL dark = TRUE;
  DwmSetWindowAttribute(hwnd_, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                        sizeof(dark));
}

void SurfaceShell::LayoutChild() {
  if (!child_) {
    return;
  }

  // PHASE 8B.6.6 — child HWND identity tracking. If Flutter recreated the
  // backing view, our child_ pointer may be stale.
  if (controller_) {
    HWND current_child = controller_->view()->GetNativeWindow();
    if (current_child != child_) {
      forensic::Log("PROJECTION WARN",
                    "child hwnd identity changed id=" + id_ +
                        " old=0x" + std::to_string(reinterpret_cast<uintptr_t>(child_)) +
                        " new=0x" + std::to_string(reinterpret_cast<uintptr_t>(current_child)));
      child_ = current_child;
      SetParent(child_, hwnd_);
    }
  }

  RECT rc{};
  GetClientRect(hwnd_, &rc);
  const int cw = rc.right - rc.left;
  const int ch = rc.bottom - rc.top;
  const int child_x = border_px_;
  const int child_y = strip_px_;
  int child_w = cw - 2 * border_px_;
  int child_h = ch - strip_px_ - border_px_;
  if (child_w < 0) child_w = 0;
  if (child_h < 0) child_h = 0;

  MoveWindow(child_, child_x, child_y, child_w, child_h, TRUE);

  // PHASE 8B.6.6 — ChildResizeTrace: verify MoveWindow actually applied.
  if (kInteractionForensics) {
    RECT actual_child{};
    GetWindowRect(child_, &actual_child);

    // Convert shell client origin to screen coords for expected-child calc.
    POINT shell_origin{0, 0};
    ClientToScreen(hwnd_, &shell_origin);
    const int exp_left   = shell_origin.x + child_x;
    const int exp_top    = shell_origin.y + child_y;

    const int act_w = static_cast<int>(actual_child.right - actual_child.left);
    const int act_h = static_cast<int>(actual_child.bottom - actual_child.top);

    const bool diverged =
        std::abs(actual_child.left - exp_left) > 2 ||
        std::abs(actual_child.top  - exp_top)  > 2 ||
        std::abs(act_w - child_w) > 2 ||
        std::abs(act_h - child_h) > 2;

    if (diverged) {
      char buf[512];
      _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                  "LayoutChild DIVERGENCE id=%s "
                  "expected_physical=(%d,%d %dx%d) "
                  "actual_physical=(%ld,%ld %dx%d) "
                  "shell_client=(%dx%d) "
                  "visible=%d parent_ok=%d",
                  id_.c_str(),
                  exp_left, exp_top, child_w, child_h,
                  actual_child.left, actual_child.top, act_w, act_h,
                  cw, ch,
                  IsWindowVisible(child_) ? 1 : 0,
                  GetParent(child_) == hwnd_ ? 1 : 0);
      forensic::Log("PROJECTION FAIL", buf);
    }
  }
}

void SurfaceShell::ApplyGeometry(const RECT& bounds) {
  // PHASE 7E rev2 — REVERTED to plain SetWindowPos on the shell.
  //
  // Rev1 used BeginDeferWindowPos(2) + child in the same batch for an
  // "atomic" shell+child move (flicker fix). Trace from a live drag showed
  // every projection logging DIVERGENCE: the OS never moved the HWND even
  // though Defer returned non-NULL and EndDeferWindowPos was called. ~700ms
  // later Windows yanked capture (WM_CAPTURECHANGED → CancelInteraction),
  // killing the drag. The exact cause of the silent Defer failure remains
  // unknown (Flutter child HWND ownership? cross-thread quirk?) — but
  // plain SetWindowPos on the parent + LayoutChild from WM_SIZE is the
  // path that worked in 7B/7C. The child still moves with the parent
  // (WS_CHILD parent-relative semantics) and resize-driven WM_SIZE fires
  // LayoutChild which MoveWindows the child explicitly. No DeferWindowPos.
  //
  // The chrome-only WM_ERASEBKGND painter from 7E rev1 stays — that's an
  // independent flicker fix and is unaffected.
  const int x = bounds.left;
  const int y = bounds.top;
  const int w = std::max(0, static_cast<int>(bounds.right - bounds.left));
  const int h = std::max(0, static_cast<int>(bounds.bottom - bounds.top));

  const BOOL ok = SetWindowPos(hwnd_, nullptr, x, y, w, h,
                                SWP_NOZORDER | SWP_NOACTIVATE);
  if (!ok && kInteractionForensics) {
    const DWORD err = GetLastError();
    forensic::Log("INTERACTION FAIL",
                  "SetWindowPos id=" + id_ + " returned FALSE err=" +
                      std::to_string(err));
  }

  if (kLogRepaintTiming) {
    forensic::Log("REPAINT",
                  "ApplyGeometry id=" + id_ +
                      " shell=(" + std::to_string(x) + "," +
                      std::to_string(y) + " " + std::to_string(w) + "x" +
                      std::to_string(h) + ")");
  }

  // REQUESTED vs ACTUAL. WM_SIZE will fire synchronously from the SetWindowPos
  // above and trigger LayoutChild for the child HWND, so by the time we read
  // GetWindowRect the shell AND child are in their final positions.
  if (kInteractionForensics) {
    RECT actual{};
    GetWindowRect(hwnd_, &actual);
    const int aw = static_cast<int>(actual.right - actual.left);
    const int ah = static_cast<int>(actual.bottom - actual.top);
    const bool diverged =
        (actual.left != x || actual.top != y || aw != w || ah != h);
    forensic::Log(diverged ? "INTERACTION FAIL" : "INTERACTION",
                  "ApplyGeometry id=" + id_ +
                      " req=(" + std::to_string(x) + "," + std::to_string(y) +
                      " " + std::to_string(w) + "x" + std::to_string(h) + ")" +
                      " actual=(" + std::to_string(actual.left) + "," +
                      std::to_string(actual.top) + " " + std::to_string(aw) +
                      "x" + std::to_string(ah) + ")" +
                      (diverged ? " — DIVERGENCE" : ""));
  }
}

LRESULT SurfaceShell::HitTest(LPARAM lparam) const {
  // SPATIAL CHROME — a chromeless surface has no drag strip or resize edges
  // (the child covers the whole window anyway, so these zones would be
  // unreachable). Everything is client.
  if (chromeless_) return HTCLIENT;

  POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
  ScreenToClient(hwnd_, &pt);
  RECT rc{};
  GetClientRect(hwnd_, &rc);

  const bool left = pt.x < border_px_;
  const bool right = pt.x >= rc.right - border_px_;
  const bool top = pt.y < border_px_;
  const bool bottom = pt.y >= rc.bottom - border_px_;

  if (top && left) return HTTOPLEFT;
  if (top && right) return HTTOPRIGHT;
  if (bottom && left) return HTBOTTOMLEFT;
  if (bottom && right) return HTBOTTOMRIGHT;
  if (left) return HTLEFT;
  if (right) return HTRIGHT;
  if (top) return HTTOP;
  if (bottom) return HTBOTTOM;
  if (pt.y < strip_px_) return HTCAPTION;  // drag strip
  return HTCLIENT;
}

LRESULT CALLBACK SurfaceShell::WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                       LPARAM lparam) noexcept {
  if (message == WM_NCCREATE) {
    auto cs = reinterpret_cast<CREATESTRUCT*>(lparam);
    SetWindowLongPtr(hwnd, GWLP_USERDATA,
                     reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  } else if (SurfaceShell* self = reinterpret_cast<SurfaceShell*>(
                 GetWindowLongPtr(hwnd, GWLP_USERDATA))) {
    return self->MessageHandler(hwnd, message, wparam, lparam);
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}

LRESULT SurfaceShell::MessageHandler(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam) noexcept {
  // 1. Shell-owned messages. Non-client behavior is suppressed here so it
  //    never reaches DefWindowProc (no native frame) or Flutter.
  switch (message) {
    case WM_NCCALCSIZE:
      // wparam==TRUE: normally we claim the whole window rect as client (no NC frame).
      if (wparam == TRUE) {
        if constexpr (morphic::probe::kQualificationProbe) {
          // 11G-PROBE — let the REAL overlapped frame (caption + sizing border) survive so
          // DWM has the qualified non-client frame it renders SYSTEMBACKDROP materials into.
          // This is the variable 2P/2Q/2R never tested. Interaction stays runtime-owned
          // (WM_SYSCOMMAND eats SC_MOVE/SC_SIZE; WM_NCLBUTTONDOWN still intercepts).
          return DefWindowProc(hwnd, message, wparam, lparam);
        } else if constexpr (kDwmFrameProbe) {
          // 11E-B.2R — let the REAL WS_THICKFRAME non-client frame SURVIVE (don't annihilate
          // it) so DWM has a frame to render the SYSTEMBACKDROP material into. DefWindowProc
          // computes the standard sizing-border inset (no caption — we never add WS_CAPTION).
          // Interaction stays runtime-owned: custom WM_NCHITTEST + WM_NCLBUTTONDOWN still
          // intercept drag/resize (return 0, no native SC_MOVE/SC_SIZE). RISK: a thin native
          // border becomes visible and the client rect is now frame-inset.
          return DefWindowProc(hwnd, message, wparam, lparam);
        } else {
          return 0;
        }
      }
      break;
    case WM_NCPAINT:
      if constexpr (morphic::probe::kQualificationProbe) {
        break;  // 11G-PROBE — let DefWindowProc paint the REAL qualified frame (no confound)
      } else {
        return 0;  // no native non-client paint
      }
    case WM_NCACTIVATE:
      if constexpr (morphic::probe::kQualificationProbe) {
        break;  // 11G-PROBE — let DefWindowProc render the real caption active/inactive state
      } else {
        return TRUE;  // no caption to redraw; keep "active" look stable
      }
    case WM_NCHITTEST: {
      LRESULT r = HitTest(lparam);
      // CHECK 1 — verify WM_NCHITTEST is firing AND returning interaction zones.
      // Logged only when the zone changes from the last seen, so cursor sitting
      // over HTCLIENT doesn't flood the log.
      if (kInteractionForensics && r != HTCLIENT && r != HTNOWHERE) {
        static thread_local LRESULT last_nc = -1;
        if (r != last_nc) {
          forensic::Log("INTERACTION",
                        "WM_NCHITTEST id=" + id_ + " zone=" +
                            std::to_string(r));
          last_nc = r;
        }
      }
      return r;
    }

    case WM_ERASEBKGND: {
      // PHASE 7E — chrome-only erase. We paint ONLY the title strip and the
      // resize borders. The child's interior (where Flutter renders) is
      // DELIBERATELY left untouched — painting it with the frame brush would
      // flash the dark band that resize flicker reports surface as. With
      // WS_CLIPCHILDREN the child's own paint is preserved; the briefly
      // exposed band during a resize stays "stale" rather than flashing.
      // SPATIAL CHROME — a chromeless surface paints NOTHING; the child fills
      // the window and there is no strip/border to erase.
      if (chromeless_) return 1;

      HDC hdc = reinterpret_cast<HDC>(wparam);
      RECT rc{};
      GetClientRect(hwnd, &rc);
      if (kLogRepaintTiming) {
        forensic::Log("REPAINT",
                      "WM_ERASEBKGND id=" + id_ + " client=(" +
                          std::to_string(rc.right) + "x" +
                          std::to_string(rc.bottom) + ")");
      }
      // Title strip (top strip_px_ px, full width).
      RECT title{0, 0, rc.right, strip_px_};
      FillRect(hdc, &title, g_frame_brush);
      // Side + bottom borders (only if there's a body beneath the strip).
      if (rc.bottom > strip_px_) {
        RECT left{0, strip_px_, border_px_, rc.bottom};
        const LONG right_x = std::max<LONG>(border_px_, rc.right - border_px_);
        RECT right{right_x, strip_px_, rc.right, rc.bottom};
        const LONG bottom_y = std::max<LONG>(strip_px_, rc.bottom - border_px_);
        RECT bottom{border_px_, bottom_y,
                    std::max<LONG>(border_px_, rc.right - border_px_),
                    rc.bottom};
        FillRect(hdc, &left, g_frame_brush);
        FillRect(hdc, &right, g_frame_brush);
        FillRect(hdc, &bottom, g_frame_brush);
      }
      return 1;  // erased
    }

    case WM_SETFOCUS:
      // Delegate keyboard focus to the Flutter child so text input reaches the
      // embedded view. The frameless shell is only a host and never keeps focus
      // itself. (This is the standard embedder delegation that the stock
      // Win32Window performs; it was lost when win32_window was removed.)
      if (child_ != nullptr) {
        SetFocus(child_);
      }
      return 0;

    // Activation: normalize every path through the router's single
    // RequestActivate (CORE_HARDENING F1) instead of calling Promote directly.
    case WM_MOUSEACTIVATE:
      if (manager_ && manager_->router()) {
        manager_->router()->RequestActivate(this);
      }
      return MA_ACTIVATE;
    case WM_ACTIVATE:
      if (LOWORD(wparam) != WA_INACTIVE && manager_ && manager_->router()) {
        manager_->router()->RequestActivate(this);
      }
      break;  // let Flutter observe activation too

    // PHASE 10.4E — titlebar/body activation coherency. The Flutter BODY is a
    // CHILD HWND; clicking it activates the child natively but, before this, never
    // reached semantic activation (only NC/titlebar messages routed through
    // RequestActivate). That split-brain is the root of "sometimes rises, sometimes
    // buried" and z weirdness. WM_PARENTNOTIFY fires on the PARENT when a child HWND
    // receives a button-down — so a click ANYWHERE on the surface (titlebar OR
    // body) now flows into the SAME semantic activation path. WM_PARENTNOTIFY is
    // generic Win32; the shell stays surface-type-agnostic.
    case WM_PARENTNOTIFY: {
      const UINT ev = LOWORD(wparam);
      if ((ev == WM_LBUTTONDOWN || ev == WM_RBUTTONDOWN ||
           ev == WM_MBUTTONDOWN) &&
          manager_ && manager_->router()) {
        manager_->router()->RequestActivate(this);
      }
      break;  // let Flutter/DefWindowProc continue
    }

    // PHASE 7A — RUNTIME-OWNED INTERACTION.
    //
    // WM_NCLBUTTONDOWN over a drag/resize zone is INTERCEPTED and routed to the
    // InteractionRouter. It is NOT passed to DefWindowProc — passing it is the
    // sole reason the OS would enter its modal SC_MOVE / SC_SIZE loop and stall
    // the message pump. We SetCapture instead, handle WM_MOUSEMOVE / WM_LBUTTONUP
    // ourselves, and return to GetMessage between events so Flutter keeps
    // rendering.
    case WM_NCLBUTTONDOWN: {
      const int hit = static_cast<int>(wparam);
      const POINT screen{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
      // CHECK 2 (gate) — log the click before anything else so we can see
      // whether the click reached us AND what zone it carries. If this line
      // never fires on a drag-strip click, the child HWND is intercepting.
      if (kInteractionForensics) {
        forensic::Log("INTERACTION",
                      "WM_NCLBUTTONDOWN id=" + id_ + " hit=" +
                          std::to_string(hit) + " screen=(" +
                          std::to_string(screen.x) + "," +
                          std::to_string(screen.y) + ")");
      }
      if (manager_ == nullptr || manager_->router() == nullptr) {
        if (kInteractionForensics) {
          forensic::Log("INTERACTION FAIL",
                        "WM_NCLBUTTONDOWN id=" + id_ +
                            " — manager/router not wired, falling back to DefWindowProc");
        }
        break;  // no router wired — fall back to default (modal loop)
      }
      if (hit == HTCAPTION) {
        // PHASE 10.5 Fix 1 — titlebar/body activation symmetry. A titlebar click
        // must reach semantic activation on the SAME unconditional path a body click
        // takes (WM_PARENTNOTIFY → RequestActivate). RequestActivate is idempotent
        // (Activate/Raise/Focus early-return when already current) and MUST run
        // BEFORE BeginDrag opens the epoch transaction, so Raise → ReconcileZOrder
        // projects immediately instead of being deferred (frozen) until drag-end.
        manager_->router()->RequestActivate(this);
        // M2.3E polish — restore-on-drag: grabbing a maximized window's titlebar pops it back to
        // size under the cursor BEFORE the drag origin is captured, so the drag then moves the
        // restored window (Windows behavior). No-op when not maximized.
        if (manager_->model() && manager_->model()->IsSurfaceMaximized(this)) {
          manager_->model()->RestoreUnderCursor(this, screen);
        }
        manager_->router()->BeginDrag(this, screen);
        return 0;  // do NOT delegate — would enter SC_MOVE modal loop
      }
      if (hit == HTLEFT || hit == HTRIGHT || hit == HTTOP || hit == HTBOTTOM ||
          hit == HTTOPLEFT || hit == HTTOPRIGHT || hit == HTBOTTOMLEFT ||
          hit == HTBOTTOMRIGHT) {
        // PHASE 10.5 Fix 1 — same activation symmetry for resize-edge clicks.
        manager_->router()->RequestActivate(this);
        manager_->router()->BeginResize(this, screen, hit);
        return 0;  // do NOT delegate — would enter SC_SIZE modal loop
      }
      break;  // other NC clicks (HTSYSMENU etc.) — let DefWindowProc handle
    }
    // M2.3E — double-click the TITLEBAR toggles SEMANTIC maximize (runtime-owned via SurfaceModel;
    // NO OS SC_MAXIMIZE, so no modal loop and no taskbar-cover). Other NC double-clicks / right-clicks
    // stay eaten so the OS doesn't try to maximize or show a system menu we don't render.
    case WM_NCLBUTTONDBLCLK:
      if (static_cast<int>(wparam) == HTCAPTION && manager_ && manager_->model()) {
        manager_->model()->ToggleMaximize(this);
      }
      return 0;
    case WM_NCRBUTTONDOWN:
    case WM_NCRBUTTONUP:
      return 0;

    // PHASE 11G-PROBE — when the overlapped frame is present it can reach the native
    // modal SC_MOVE/SC_SIZE loops (Alt+Space menu, caption interactions DefWindowProc
    // doesn't route through our WM_NCLBUTTONDOWN). 7A owns drag/resize unconditionally,
    // so eat exactly those two so the OS can NEVER stall our pump. Everything else
    // (SC_MINIMIZE/SC_CLOSE/etc.) passes through. Probe-only: when the flag is off this
    // case is an inert `break` (identical to having no case — falls to Flutter/DefWindowProc).
    case WM_SYSCOMMAND:
      if constexpr (morphic::probe::kQualificationProbe) {
        const WPARAM cmd = wparam & 0xFFF0;
        if (cmd == SC_MOVE || cmd == SC_SIZE) {
          forensic::Log("11G-PROBE", "WM_SYSCOMMAND suppressed (SC_MOVE/SC_SIZE) id=" + id_);
          return 0;  // do NOT enter the native modal move/size loop
        }
      }
      break;

    case WM_MOUSEMOVE: {
      // Captured mouse moves arrive here in CLIENT coords; convert to screen
      // and let the router compute new bounds. Outside an interaction, fall
      // through to Flutter (controller_->HandleTopLevelWindowProc below) so
      // hover/pointer logic still works on the shell strip.
      InteractionRouter* router = manager_ ? manager_->router() : nullptr;
      if (router && router->mode() != InteractionMode::None &&
          router->interacting() == this) {
        POINT pt{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ClientToScreen(hwnd, &pt);
        router->UpdatePointer(pt);
        return 0;
      }
      break;
    }
    case WM_LBUTTONUP: {
      InteractionRouter* router = manager_ ? manager_->router() : nullptr;
      if (router && router->mode() != InteractionMode::None &&
          router->interacting() == this) {
        router->EndInteraction();
        return 0;
      }
      break;
    }
    // PHASE 7C — frame ticks have moved to the runtime FrameClock (own
    // message-only HWND). Surface shells no longer host any timer; if WM_TIMER
    // ever arrives here it's a plugin timer and falls through to Flutter.
    case WM_CAPTURECHANGED: {
      // Capture was reassigned (lparam = new capture HWND). If it's not us
      // and we still think we're interacting, the OS pulled the rug out —
      // bail out cleanly so we don't leak the transaction or interaction state.
      HWND new_capture = reinterpret_cast<HWND>(lparam);
      InteractionRouter* router = manager_ ? manager_->router() : nullptr;
      if (router && router->mode() != InteractionMode::None &&
          router->interacting() == this && new_capture != hwnd) {
        forensic::Log("SHELL", "WM_CAPTURECHANGED id=" + id_ +
                                   " -> CancelInteraction");
        router->CancelInteraction(this, "capture-changed");
      }
      return 0;
    }

    case WM_SIZE:
      // M2.3E — if the OS restored us from the taskbar while we were semantically minimized, sync
      // the model (visibility + window-state projection). SIZE_RESTORED also fires from our own
      // geometry projection; NoteNativeRestored is a no-op unless we actually held Minimized.
      if (wparam == SIZE_RESTORED && manager_ && manager_->model()) {
        manager_->model()->NoteNativeRestored(this);
      }
      // PHASE 7E rev2 — always LayoutChild. With the SetWindowPos-only
      // ApplyGeometry (atomic Defer reverted), WM_SIZE fires synchronously
      // from our own projection AND from OS-initiated paths (DPI change,
      // ShowWindow restore). In both cases we want the Flutter child to
      // catch up to the new client area. LayoutChild is idempotent.
      if (kLogRepaintTiming) {
        forensic::Log("REPAINT", "WM_SIZE id=" + id_ + " -> LayoutChild");
      }
      LayoutChild();
      return 0;
    case WM_DPICHANGED: {
      auto new_rect = reinterpret_cast<RECT*>(lparam);
      // PHASE 8B-prep — I-A5 closure. If we're mid-interaction, recapture
      // the router's origin BEFORE projecting the new bounds, so the next
      // pointer event computes delta=0 against the post-DPI baseline instead
      // of jumping by the scale-change. RecaptureOrigin is a no-op when
      // not interacting, so unconditional call is safe.
      if (manager_ && manager_->router() &&
          manager_->router()->mode() != InteractionMode::None &&
          manager_->router()->interacting() == this) {
        forensic::Log("ROUTER",
                      "WM_DPICHANGED during interaction id=" + id_ +
                          " — recapturing router origin");
        manager_->router()->OnDpiChanged(this, *new_rect);
      }
      if (manager_ && manager_->model()) {
        manager_->model()->SetBounds(this, *new_rect);
      } else {
        SetWindowPos(hwnd, nullptr, new_rect->left, new_rect->top,
                     new_rect->right - new_rect->left,
                     new_rect->bottom - new_rect->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
      }
      return 0;
    }
    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;
    case WM_DESTROY:
      // Release the Flutter engine HERE — while this HWND and the engine's child
      // view are still alive — NOT in ~SurfaceShell, which runs at Reap AFTER the
      // window is gone. Tearing the FlutterViewController down against a dead
      // window crashes inside flutter_windows.dll under churn (found by the churn
      // harness). reset() is idempotent, so ~SurfaceShell's reset becomes a no-op.
      //
      // R0 — at app exit this is the owner-chain cascade (shell_root WM_CLOSE →
      // DestroyWindow → every owned render_source lands here). LEAK instead of
      // reset so the cascade doesn't fire N teardown-race crashes on the way out;
      // process termination reclaims the engines.
      ReleaseOrLeakController(controller_, id_);
      // If this surface is being torn down mid-interaction, cancel cleanly so
      // the router doesn't hold a dangling interacting_ pointer and the
      // transaction is committed (z/activation/events flushed).
      if (manager_ && manager_->router()) {
        manager_->router()->CancelInteraction(this, "destroy");
      }
      if (manager_) manager_->OnSurfaceDestroyed(this);
      return 0;
    case WM_NCDESTROY:
      // F6: the HWND is being finalized. Null our member so ~SurfaceShell does
      // not DestroyWindow a stale handle (double-destroy). Still let
      // DefWindowProc do the terminal non-client cleanup (via the `hwnd` param).
      hwnd_ = nullptr;
      return DefWindowProc(hwnd, message, wparam, lparam);
  }

  // 2. Let the Flutter engine handle remaining top-level messages.
  if (controller_) {
    if (std::optional<LRESULT> result =
            controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                  lparam)) {
      return *result;
    }
  }

  // 3. Default.
  return DefWindowProc(hwnd, message, wparam, lparam);
}
