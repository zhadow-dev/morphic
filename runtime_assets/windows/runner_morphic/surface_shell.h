#ifndef RUNNER_SURFACE_SHELL_H_
#define RUNNER_SURFACE_SHELL_H_

#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>

#include <memory>
#include <string>

class SurfaceManager;

// PHASE 2 / PHASE 7A — SurfaceShell
//
// A frameless, compositor-managed surface window. It is a WS_POPUP with NO
// native caption, border, system menu, shadow, or rounded corners. It hosts
// exactly one FlutterViewController (its own engine) as a child HWND inset
// below a custom drag strip and inside custom resize borders.
//
// AUTHORITY: a thin Win32 adapter.
//   - owns its HWND + WndProc
//   - suppresses ALL native non-client rendering
//   - custom hit-testing CLASSIFIES drag/resize zones for cursor selection only
//   - WM_NCLBUTTONDOWN over a drag/resize zone is INTERCEPTED (not passed to
//     DefWindowProc) and routed to InteractionRouter — this is what kills the
//     OS modal SC_MOVE/SC_SIZE loops and unblocks the message pump
//   - forwards captured WM_MOUSEMOVE/WM_LBUTTONUP/WM_CAPTURECHANGED to the
//     router for the duration of an interaction
//   - reports activation to its SurfaceManager's router (single activation path)
//
// It does NOT own z-order, the message loop, sibling surfaces, interaction
// state, drag/resize geometry, or pointer capture. Those belong to the router.
class SurfaceShell {
 public:
  SurfaceShell(SurfaceManager* manager, std::string id);
  ~SurfaceShell();

  SurfaceShell(const SurfaceShell&) = delete;
  SurfaceShell& operator=(const SurfaceShell&) = delete;

  // Create the popup window and a Flutter engine running `entrypoint`
  // (a @pragma('vm:entry-point') Dart function). Window is created hidden.
  //
  // PHASE 10.3 — `ex_style` is the Win32 extended window style applied at creation
  // (default WS_EX_APPWINDOW = a task-switchable app citizen, as before). `owner`
  // is the Win32 owner HWND (default null = unowned top-level). BOTH are PLAIN
  // Win32 values handed down by the product policy layer — the shell stays
  // surface-type-AGNOSTIC: it applies the flags it's given and never learns
  // SurfaceKind. (Sacred firewall: arrow points DOWN only.)
  //
  // SPATIAL CHROME — one more plain value, same pattern:
  //   `chromeless` — paint NO title strip / borders and let the Flutter child
  //                  fill the whole window. The drag strip and resize edges
  //                  disappear with the chrome (the child covers them), so a
  //                  chromeless surface is moved/resized programmatically, not
  //                  by grab. For spatial-scene surfaces that author their own
  //                  look.
  //
  // NOTE (settled): arbitrary corner radii are NOT a shell concern. SetWindowRgn
  // was tried and is a dead end on this substrate — a window region does not
  // clip the DWM-composited output (the ACCENT backdrop stays rectangular) and
  // degrades the glass. Window shape comes from CONTENT ALPHA over the
  // full-glass frame; the radius is a policy-layer surface property delivered
  // to the surface's engine (see SurfaceEcology + 'surface.chrome').
  bool Create(const flutter::DartProject& base_project,
              const std::string& entrypoint, int x, int y, int width,
              int height, DWORD ex_style = WS_EX_APPWINDOW,
              HWND owner = nullptr, bool chromeless = false);

  // Show without stealing activation.
  void Show();

  HWND GetHandle() const { return hwnd_; }
  // PHASE 8B.6.6 — expose the Flutter child HWND for projection coherence
  // auditing. The child is no longer an opaque implementation detail; its
  // geometry is now part of projection correctness verification.
  HWND child_hwnd() const { return child_; }
  const std::string& id() const { return id_; }

  // PHASE 11 — release the Flutter engine (the FlutterViewController) while this
  // surface's HWND + child view are still alive. The deferred-teardown reaper
  // calls this — serialized + quiesced — so flutter_windows.dll's worker-thread
  // shutdown doesn't race. Idempotent.
  void ReleaseEngine();

  // SPATIAL MIGRATION Stage 2A — expose this surface's engine messenger so the
  // product-layer PlaneVisualProjector can push per-surface plane-visual state over a
  // 'morphic/plane' channel. Read-only/neutral (like child_hwnd()): the shell hands out an
  // existing capability and learns NOTHING about planes (firewall intact). Null pre-engine.
  flutter::BinaryMessenger* messenger() const {
    return controller_ ? controller_->engine()->messenger() : nullptr;
  }

  // PHASE 7E — atomic shell+child geometry commit via DeferWindowPos. Called
  // by SurfaceModel::SetBounds (which delegates ALL geometry projection to the
  // shell so the shell can batch its own chrome with the child's inset move).
  // The redundant LayoutChild that would otherwise fire from WM_SIZE is
  // suppressed by in_apply_geometry_ while this is on the stack.
  void ApplyGeometry(const RECT& bounds);

 private:
  static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                  LPARAM lparam) noexcept;
  LRESULT MessageHandler(HWND hwnd, UINT message, WPARAM wparam,
                         LPARAM lparam) noexcept;

  // Custom WM_NCHITTEST: maps the cursor to drag (HTCAPTION) / resize (HT*)
  // zones, else HTCLIENT.
  LRESULT HitTest(LPARAM lparam) const;

  // Size the Flutter child into the inset content area.
  void LayoutChild();

  // Sharp corners + dark mode (no rounding, no Mica, no frame).
  void ConfigureDwm();

  // Log the taskbar / Alt-Tab eligibility determinants (PHASE 2.5).
  void LogParticipation() const;

  // PHASE 7E-Reg — runs after the first frame. Verifies the structural
  // invariants that runtime-owned interaction depends on: child does NOT
  // overlap the title strip, WS_CLIPCHILDREN is still set, HitTest at a known
  // strip point returns HTCAPTION (not HTCLIENT). Failures log as
  // INTERACTION SELFCHECK FAIL so they're impossible to miss in the trace.
  void RunInteractionSelfCheck();

  static void EnsureWindowClass();

  SurfaceManager* manager_;
  std::string id_;
  HWND hwnd_ = nullptr;
  HWND child_ = nullptr;
  std::unique_ptr<flutter::FlutterViewController> controller_;

  // SPATIAL CHROME — chrome metrics as members so a chromeless surface can zero
  // them. Initialized from the class constants at Create.
  bool chromeless_ = false;
  int strip_px_ = 0;   // title-strip height (0 when chromeless)
  int border_px_ = 0;  // resize-border thickness (0 when chromeless)
};

#endif  // RUNNER_SURFACE_SHELL_H_
