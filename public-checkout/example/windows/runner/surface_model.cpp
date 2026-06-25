#include "surface_model.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "compositor/compositor_runtime.h"
#include "forensic_trace.h"
#include "runtime_events.h"
#include "surface_shell.h"

namespace {
std::string IdOf(SurfaceShell* s);  // fwd

// ===========================================================================
// DESKTOP-SPACE REACHABILITY INVARIANT (runtime spatial law)
// ===========================================================================
//
// The runtime's first explicit desktop-space invariant: a surface can never be
// lost. Geometry projected through SurfaceModel is clamped so the surface's drag
// handle (title strip) always remains reachable on a monitor work area. This is
// NOT a one-off bug patch — it is a layered law (policy + reason + phase +
// telemetry + recovery semantics) that extraction / docking / grouping /
// tethering / persisted-layout restore will all depend on.
//
// SCOPE GUARDRAIL (hard): reachability ONLY. NO snap, docking, edge magnetism,
// auto-layout, inertia, or resistance curves — those are not geometry policy.
// The clamp's only job is "keep the grab handle reachable, minimally."
//
// The invariant is POST-PROJECTION reachability, not microstate purity: each
// SetBounds is clamped, so transient off-screen states (resize tick, DPI
// transition, monitor unplug) are corrected by the time projection settles.
// Recovery MINIMALLY perturbs geometry — clamp to the nearest valid position,
// never recenter.

// Reachability anchor constants. Mirror surface_shell.cpp's anon-namespace
// kTitleStrip (30) / kBorder (6); duplicated here (with this comment as the
// sync link) rather than coupling the model to shell internals.
constexpr LONG kTitleStripClamp = 30;  // full strip height must stay grabbable
constexpr LONG kMinVisible = 48;       // min horizontal band of the strip on-screen

enum class ClampPolicy {
  None,          // pass-through (deliberate off-screen staging — unused today)
  Reachable,     // grab-strip top edge must stay on a work area (IMPLEMENTED)
  FullyVisible,  // whole window inside work area (reserved — overlays/docking)
};

enum class ClampReason {
  DragOverflow,
  ResizeOverflow,
  ActivationRecovery,
  MonitorTopologyChange,
  DPIRecovery,
  PersistedLayoutRestore,
};

// Preventative (clamp during a live projection) vs corrective (clamp during
// activation because the window was ALREADY off-screen). The key signal for
// "when did the runtime lose spatial coherence."
enum class ClampPhase {
  Projection,
  Recovery,
};

// Self-describing so telemetry correlation is trivial.
struct ClampResult {
  RECT rect;        // clamped (final)
  RECT original;    // requested, pre-clamp
  RECT work_area;   // selected monitor rcWork
  ClampPolicy policy = ClampPolicy::Reachable;
  ClampReason reason = ClampReason::ResizeOverflow;
  ClampPhase phase = ClampPhase::Projection;
  bool applied = false;  // rect != original
  std::wstring device;   // monitor device name (MONITORINFOEX.szDevice)
};

const char* ClampReasonStr(ClampReason r) {
  switch (r) {
    case ClampReason::DragOverflow:           return "drag_overflow";
    case ClampReason::ResizeOverflow:         return "resize_overflow";
    case ClampReason::ActivationRecovery:     return "activation_recovery";
    case ClampReason::MonitorTopologyChange:  return "monitor_topology_change";
    case ClampReason::DPIRecovery:            return "dpi_recovery";
    case ClampReason::PersistedLayoutRestore: return "persisted_layout_restore";
  }
  return "?";
}

const char* ClampPhaseStr(ClampPhase p) {
  return p == ClampPhase::Recovery ? "recovery" : "projection";
}

// Narrow MONITORINFOEX.szDevice (WCHAR) to UTF-8-ish ASCII for the log line.
std::string NarrowDevice(const wchar_t* dev) {
  std::string out;
  if (!dev) return out;
  for (const wchar_t* p = dev; *p; ++p) {
    out.push_back(static_cast<char>(*p < 128 ? *p : '?'));
  }
  return out;
}

// Clamp `r` so the title-strip drag handle stays reachable on the nearest
// monitor work area. Width/height are PRESERVED — only position is nudged, and
// minimally (clamp to nearest valid origin; never recenter).
ClampResult ClampToWorkArea(RECT r, ClampPolicy policy, ClampReason reason,
                            ClampPhase phase) {
  ClampResult result;
  result.original = r;
  result.rect = r;
  result.policy = policy;
  result.reason = reason;
  result.phase = phase;

  if (policy == ClampPolicy::None) {
    return result;  // pass-through
  }

  // Nearest monitor — handles multi-monitor + fully-off-screen rects.
  HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
  MONITORINFOEXW mi{};
  mi.cbSize = sizeof(mi);
  if (mon == nullptr || !GetMonitorInfoW(mon, &mi)) {
    return result;  // can't resolve a monitor; leave geometry untouched
  }
  const RECT work = mi.rcWork;
  result.work_area = work;
  result.device = mi.szDevice;

  const LONG w = r.right - r.left;
  const LONG h = r.bottom - r.top;

  RECT c = r;

  if (policy == ClampPolicy::FullyVisible) {
    // Reserved path: keep the entire window inside the work area.
    c.left = (std::max)(work.left, (std::min)(c.left, work.right - w));
    c.top = (std::max)(work.top, (std::min)(c.top, work.bottom - h));
  } else {
    // Reachable (default): the TITLE-STRIP TOP EDGE must stay grabbable.
    //   top  ∈ [work.top, work.bottom - kTitleStripClamp]  (full strip on-screen,
    //         never above the work-area top, never sunk past its bottom)
    //   left ∈ [work.left - (w - kMinVisible), work.right - kMinVisible]
    //         (at least kMinVisible px of the strip's width stays inside)
    const LONG min_left = work.left - (w - kMinVisible);
    const LONG max_left = work.right - kMinVisible;
    c.left = (std::max)(min_left, (std::min)(c.left, max_left));
    const LONG max_top = work.bottom - kTitleStripClamp;
    c.top = (std::max)(work.top, (std::min)(c.top, max_top));
  }

  // Preserve size — translate only.
  c.right = c.left + w;
  c.bottom = c.top + h;
  result.rect = c;
  result.applied =
      (c.left != r.left || c.top != r.top || c.right != r.right ||
       c.bottom != r.bottom);
  return result;
}

// Emit a [GEOM CLAMP] line directly from the self-describing result. Only call
// when result.applied.
void LogClamp(const std::string& id, const ClampResult& cr) {
  char buf[416];
  _snprintf_s(
      buf, sizeof(buf), _TRUNCATE,
      "id=%s reason=%s phase=%s req=(%ld,%ld %ldx%ld) -> clamped=(%ld,%ld %ldx%ld) "
      "monitor=%s work=(%ld,%ld %ldx%ld)",
      id.c_str(), ClampReasonStr(cr.reason), ClampPhaseStr(cr.phase),
      cr.original.left, cr.original.top, cr.original.right - cr.original.left,
      cr.original.bottom - cr.original.top, cr.rect.left, cr.rect.top,
      cr.rect.right - cr.rect.left, cr.rect.bottom - cr.rect.top,
      NarrowDevice(cr.device.c_str()).c_str(), cr.work_area.left,
      cr.work_area.top, cr.work_area.right - cr.work_area.left,
      cr.work_area.bottom - cr.work_area.top);
  forensic::Log("GEOM CLAMP", buf);
}

// PHASE 10.5c — z-projection forensics (INVESTIGATION; log-only, flag-gated; flip to
// false to silence). Dumps OUR surfaces in NATIVE z order (top→bottom), annotated with
// each one's SEMANTIC index and OWNER-by-id, so an owner-cluster sibling reshuffle is
// visible. This is the runtime confirmation of the owner-BLIND z divergence (audit
// Confirmed Fact 5): the model can request an owned window BELOW its owner, which the
// OS silently overrides — and that override is the reshuffle the user feels.
constexpr bool kLogZProjection = true;

void DumpNativeZ(const char* tag, const std::vector<SurfaceShell*>& z) {
  if (!kLogZProjection) return;
  auto id_of_hwnd = [&z](HWND h) -> std::string {
    for (SurfaceShell* s : z)
      if (s && s->GetHandle() == h) return s->id();
    return std::string();
  };
  auto sem_of_hwnd = [&z](HWND h) -> int {
    for (size_t i = 0; i < z.size(); ++i)
      if (z[i] && z[i]->GetHandle() == h) return static_cast<int>(i);
    return -1;
  };
  std::string line;
  // Walk the OS z-order top→bottom; emit only our surfaces, in native order.
  for (HWND h = GetTopWindow(nullptr); h != nullptr; h = GetWindow(h, GW_HWNDNEXT)) {
    const int sem = sem_of_hwnd(h);
    if (sem < 0) continue;  // not one of ours
    const bool topmost = (GetWindowLong(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    const std::string owner = id_of_hwnd(GetWindow(h, GW_OWNER));
    if (!line.empty()) line += "  >  ";
    line += id_of_hwnd(h) + "(sem=" + std::to_string(sem) +
            (topmost ? " topmost" : "") +
            (owner.empty() ? "" : " owner=" + owner) + ")";
  }
  forensic::Log("ZPROJ", std::string(tag) + ": " + line);
}
}  // namespace

void SurfaceModel::Add(SurfaceShell* surface) {
  if (surface == nullptr) {
    return;
  }
  z_order_.insert(z_order_.begin(), surface);  // new surfaces enter on top
  visibility_[surface] = VisibilityState::Hidden;
  // Snapshot current native bounds as the initial semantic bounds. From here on
  // SetBounds is the single mutator; native HWND geometry is the projection.
  RECT rc{};
  if (surface->GetHandle() && GetWindowRect(surface->GetHandle(), &rc)) {
    bounds_[surface] = rc;
  }
}

void SurfaceModel::Remove(SurfaceShell* surface) {
  z_order_.erase(std::remove(z_order_.begin(), z_order_.end(), surface),
                 z_order_.end());
  visibility_.erase(surface);
  bounds_.erase(surface);
  restore_bounds_.erase(surface);  // M2.3E — drop window-state with the surface
  maximized_.erase(surface);
  if (active_ == surface) active_ = nullptr;
  if (focused_ == surface) focused_ = nullptr;
}

void SurfaceModel::Raise(SurfaceShell* surface) {
  // PHASE 8B-prep — I-A4 reentrancy guard. Bus subscribers of ZORDER-derived
  // events could re-enter Raise on a different surface; drop+count instead of
  // recursing.
  if (raising_) {
    ++reentrant_drops_;
    forensic::Log("ZORDER", "REENTRANT Raise DROPPED id=" + IdOf(surface) +
                                " total_drops=" +
                                std::to_string(reentrant_drops_));
    return;
  }
  if (z_order_.empty() || z_order_.front() == surface) {
    return;  // already semantic top
  }
  auto it = std::find(z_order_.begin(), z_order_.end(), surface);
  if (it == z_order_.end()) {
    return;
  }
  raising_ = true;
  z_order_.erase(it);
  z_order_.insert(z_order_.begin(), surface);
  forensic::Log("ZORDER", "Raise id=" + IdOf(surface) + " -> semantic top");
  if (txn_active_) {
    z_dirty_ = true;  // freeze: project once at commit
    forensic::Log("TXN", "z reconcile deferred (interaction active)");
  } else {
    ReconcileZOrder();
  }
  raising_ = false;
}

void SurfaceModel::Activate(SurfaceShell* surface) {
  // PHASE 8B-prep — I-A4 reentrancy guard.
  if (activating_) {
    ++reentrant_drops_;
    forensic::Log("ACTIVATION",
                  "REENTRANT Activate DROPPED id=" + IdOf(surface) +
                      " total_drops=" + std::to_string(reentrant_drops_));
    return;
  }
  if (active_ == surface) {
    return;  // single source of truth; no double-fire
  }
  activating_ = true;
  active_ = surface;
  forensic::Log("ACTIVATION", "Activate id=" + IdOf(surface));
  if (txn_active_) {
    activation_dirty_ = true;  // freeze: project foreground once at commit
  } else {
    ProjectActivation();
  }
  PublishOrDefer(RuntimeEvent::SurfaceActivated, surface);
  activating_ = false;
}

void SurfaceModel::Focus(SurfaceShell* surface) {
  // PHASE 8B-prep — I-A4 reentrancy guard.
  if (focusing_) {
    ++reentrant_drops_;
    forensic::Log("FOCUS", "REENTRANT Focus DROPPED id=" + IdOf(surface) +
                              " total_drops=" +
                              std::to_string(reentrant_drops_));
    return;
  }
  if (focused_ == surface) {
    return;
  }
  focusing_ = true;
  focused_ = surface;
  forensic::Log("FOCUS", "Focus id=" + IdOf(surface));
  // This records the SEMANTIC keyboard owner. The actual native delegation
  // (top-level focus → the FlutterView child, so text input works) is performed
  // per-shell in SurfaceShell's WM_SETFOCUS handler — NOT here. Cross-surface
  // focus policy (forcing focus to a non-active surface) is future work.
  PublishOrDefer(RuntimeEvent::SurfaceFocused, surface);
  focusing_ = false;
}

void SurfaceModel::Show(SurfaceShell* surface) {
  SetVisibility(surface, VisibilityState::Shown);
}

void SurfaceModel::SetVisibility(SurfaceShell* surface, VisibilityState vis) {
  if (surface == nullptr) {
    return;
  }
  visibility_[surface] = vis;
  ProjectVisibility(surface);
}

bool SurfaceModel::IsSurfaceMaximized(SurfaceShell* surface) const {
  auto it = maximized_.find(surface);
  return it != maximized_.end() && it->second;
}

bool SurfaceModel::IsSurfaceMinimized(SurfaceShell* surface) const {
  auto it = visibility_.find(surface);
  return it != visibility_.end() && it->second == VisibilityState::Minimized;
}

void SurfaceModel::Minimize(SurfaceShell* surface) {
  if (surface == nullptr) return;
  // Native visibility state. Owned toolwindows (palette/inspector) hide with their owner, so an
  // editor minimize compound-minimizes for free via the Win32 owner chain.
  SetVisibility(surface, VisibilityState::Minimized);
  forensic::Log("WINDOW", "Minimize id=" + IdOf(surface));
  if (on_window_state_) on_window_state_(surface);
}

void SurfaceModel::Maximize(SurfaceShell* surface) {
  if (surface == nullptr || surface->GetHandle() == nullptr || IsSurfaceMaximized(surface)) return;
  // SEMANTIC maximize: remember the current rect, then SetBounds to the monitor WORK AREA (respects
  // the taskbar; never full-monitor). No SW_MAXIMIZE — the projection path is unchanged, so nothing
  // un-maximizes us and the auditor sees model == native.
  const RECT cur = bounds(surface);
  restore_bounds_[surface] = cur;
  RECT probe = cur;
  HMONITOR mon = MonitorFromRect(&probe, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  if (mon == nullptr || !GetMonitorInfo(mon, &mi)) return;  // can't resolve monitor — abort cleanly
  maximized_[surface] = true;
  SetBounds(surface, mi.rcWork);
  forensic::Log("WINDOW", "Maximize id=" + IdOf(surface) + " -> work area");
  if (on_window_state_) on_window_state_(surface);
}

void SurfaceModel::Restore(SurfaceShell* surface) {
  if (surface == nullptr) return;
  bool changed = false;
  // Un-minimize (native). SW_RESTORE de-iconifies; SW_SHOWNA would not.
  if (IsSurfaceMinimized(surface)) {
    visibility_[surface] = VisibilityState::Shown;
    if (surface->GetHandle()) ShowWindow(surface->GetHandle(), SW_RESTORE);
    changed = true;
  }
  // Un-maximize (semantic): return to the saved rect.
  if (IsSurfaceMaximized(surface)) {
    maximized_[surface] = false;
    auto it = restore_bounds_.find(surface);
    if (it != restore_bounds_.end()) SetBounds(surface, it->second);
    changed = true;
  }
  if (changed) {
    forensic::Log("WINDOW", "Restore id=" + IdOf(surface));
    if (on_window_state_) on_window_state_(surface);
  }
}

void SurfaceModel::ToggleMaximize(SurfaceShell* surface) {
  if (IsSurfaceMaximized(surface)) {
    Restore(surface);
  } else {
    Maximize(surface);
  }
}

void SurfaceModel::RestoreUnderCursor(SurfaceShell* surface, POINT screen) {
  if (surface == nullptr || !IsSurfaceMaximized(surface)) return;
  const RECT cur = bounds(surface);  // the maximized (work-area) rect
  auto it = restore_bounds_.find(surface);
  const LONG rw = (it != restore_bounds_.end()) ? (it->second.right - it->second.left)
                                                : (cur.right - cur.left) / 2;
  const LONG rh = (it != restore_bounds_.end()) ? (it->second.bottom - it->second.top)
                                                : (cur.bottom - cur.top) / 2;
  const LONG mw = cur.right - cur.left;
  // Keep the cursor at the SAME horizontal fraction along the (now smaller) titlebar.
  double ratio = mw > 0 ? static_cast<double>(screen.x - cur.left) / mw : 0.5;
  ratio = (std::max)(0.0, (std::min)(1.0, ratio));
  RECT r;
  r.left = screen.x - static_cast<LONG>(ratio * rw);
  r.top = screen.y - 10;  // titlebar sits just under the cursor
  r.right = r.left + rw;
  r.bottom = r.top + rh;
  maximized_[surface] = false;
  SetBounds(surface, r);  // clamp keeps the grab handle on-screen
  forensic::Log("WINDOW", "RestoreUnderCursor (drag) id=" + IdOf(surface));
  if (on_window_state_) on_window_state_(surface);
}

void SurfaceModel::NoteNativeRestored(SurfaceShell* surface) {
  // OS restored us (e.g. taskbar) while we held Minimized — sync semantic visibility + re-project.
  if (surface == nullptr) return;
  auto it = visibility_.find(surface);
  if (it != visibility_.end() && it->second == VisibilityState::Minimized) {
    visibility_[surface] = VisibilityState::Shown;
    forensic::Log("WINDOW", "NativeRestored id=" + IdOf(surface));
    if (on_window_state_) on_window_state_(surface);
  }
}

void SurfaceModel::SetBounds(SurfaceShell* surface, const RECT& bounds) {
  if (surface == nullptr || surface->GetHandle() == nullptr) {
    return;
  }
  // PHASE 7B — reentrancy guard. SetWindowPos below synchronously fans out
  // WM_WINDOWPOSCHANGED → WM_MOVE / WM_SIZE on this same WndProc. If anything
  // on that synchronous stack calls back into SetBounds, drop it (counter) so
  // bounds_ stays single-writer and we don't recurse into a SetWindowPos storm.
  if (projecting_) {
    ++reentrant_drops_;
    forensic::Log("GEOM", "REENTRANT SetBounds DROPPED id=" + IdOf(surface) +
                              " total_drops=" + std::to_string(reentrant_drops_));
    return;
  }
  // DESKTOP-SPACE REACHABILITY INVARIANT. Every projection path flows through
  // here (interaction sessions, DPI changes, future programmatic moves, and —
  // by contract — any future persisted-layout restore), so the grab handle can
  // never drift off-screen regardless of caller. Clamp is preventative here
  // (ClampPhase::Projection); only POSITION is nudged, minimally, never resized.
  // The interaction layer's geometry math stays pure cursor semantics — this is
  // the single enforcement point for the spatial law.
  const ClampResult clamp = ClampToWorkArea(
      bounds, ClampPolicy::Reachable, ClampReason::ResizeOverflow,
      ClampPhase::Projection);
  if (clamp.applied) {
    LogClamp(IdOf(surface), clamp);
  }
  const RECT projected = clamp.rect;
  bounds_[surface] = projected;
  // SINGLE projection authority. The SHELL is now the geometry MECHANISM
  // (PHASE 7E — atomic shell+child via DeferWindowPos to kill resize-flicker);
  // the MODEL still owns the semantic state + reentrancy guard + transaction
  // integration. ApplyGeometry uses SWP_NOZORDER|SWP_NOACTIVATE internally so
  // the transaction's frozen z/activation epoch is still honored.
  projecting_ = true;
  // PHASE 7C-T: test injection point. Any installed callback fires HERE — inside
  // the projection epoch — so a reentrant SetBounds call from it hits the guard
  // above. No-op in production.
  if (test_projection_callback_) {
    test_projection_callback_();
  }
  // PHASE 9 — route the native hand-off through the projection seam. The model
  // still owns semantic truth (bounds_, clamp, reentrancy guard above); only the
  // final native projection goes through the CompositorRuntime (HWND backend =
  // ApplyGeometry, byte-identical). Null fallback keeps the direct path for
  // rollback / pre-wiring.
  if (compositor_) {
    compositor_->Project(surface, projected);
  } else {
    surface->ApplyGeometry(projected);
  }
  projecting_ = false;
  forensic::Log("GEOM", "SetBounds id=" + IdOf(surface) + " rect=(" +
                            std::to_string(projected.left) + "," +
                            std::to_string(projected.top) + " " +
                            std::to_string(projected.right - projected.left) + "x" +
                            std::to_string(projected.bottom - projected.top) + ")");
}

RECT SurfaceModel::SetSemanticBounds(SurfaceShell* surface, const RECT& bounds) {
  // PHASE 8E — semantic truth WITHOUT native projection (the settle seam). Same
  // reachability clamp as SetBounds so semantic bounds_ can never become
  // off-screen, but NO ApplyGeometry — the PresentationCoordinator drives the
  // native HWND while it eases toward this rect. The reentrancy guard is NOT
  // engaged here because we never call ApplyGeometry (which is what fans out the
  // WM_WINDOWPOSCHANGED that the guard protects against).
  if (surface == nullptr) return RECT{};
  const ClampResult clamp = ClampToWorkArea(
      bounds, ClampPolicy::Reachable, ClampReason::ResizeOverflow,
      ClampPhase::Projection);
  if (clamp.applied) LogClamp(IdOf(surface), clamp);
  bounds_[surface] = clamp.rect;
  return clamp.rect;
}

RECT SurfaceModel::bounds(SurfaceShell* surface) const {
  auto it = bounds_.find(surface);
  if (it != bounds_.end()) {
    return it->second;
  }
  RECT rc{};
  if (surface && surface->GetHandle()) {
    GetWindowRect(surface->GetHandle(), &rc);
  }
  return rc;
}

void SurfaceModel::ReconcileZOrder() {
  DumpNativeZ("reconcile PRE", z_order_);  // PHASE 10.5c — investigation
  // PHASE 9 — z PROJECTION routes through the seam (the compositor MIRRORS this
  // order onto the backend; SurfaceModel still DECIDES the order — z_order_ is
  // truth). Null fallback keeps the direct SetWindowPos loop.
  if (compositor_) {
    compositor_->ReconcileZ(z_order_);
    DumpNativeZ("reconcile POST", z_order_);  // PHASE 10.5c
    return;
  }
  // Direct fallback (no compositor): topmost-aware, mirroring the backend (PHASE
  // 10.4D/F). Reconcile each band internally — non-topmost vs HWND_TOP, topmost vs
  // HWND_TOPMOST — so overlays stay above. WS_EX_TOPMOST is read from the HWND
  // (generic Win32). SWP_NOACTIVATE.
  HWND prev_normal = HWND_TOP;
  HWND prev_topmost = HWND_TOPMOST;
  for (SurfaceShell* s : z_order_) {
    if (s == nullptr || s->GetHandle() == nullptr) continue;
    const HWND h = s->GetHandle();
    const bool topmost = (GetWindowLong(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    HWND& anchor = topmost ? prev_topmost : prev_normal;
    SetWindowPos(h, anchor, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    anchor = h;
  }
  DumpNativeZ("reconcile POST", z_order_);  // PHASE 10.5c
}

void SurfaceModel::ProjectActivation() {
  // Activation is a REACHABILITY GUARANTEE, not just z-promotion. Activating a
  // surface (e.g. via Alt-Tab) must make it visible + on-screen + foreground —
  // defense-in-depth so even if some path drove a window off-screen, the user
  // can always recover it. Ordering matters: ShowWindow can itself mutate the
  // window's projected rect (minimized restore / shell / DPI), so we FETCH the
  // live rect AFTER showing, THEN clamp (corrective, ClampPhase::Recovery),
  // THEN foreground.
  if (active_ == nullptr) return;
  HWND handle = active_->GetHandle();
  if (handle == nullptr) return;

  // 1. Ensure shown. If our semantic visibility says Hidden, show without
  //    activating (SW_SHOWNA) — foreground is asserted explicitly below.
  auto vis_it = visibility_.find(active_);
  if (vis_it != visibility_.end() && vis_it->second == VisibilityState::Hidden) {
    ShowWindow(handle, SW_SHOWNA);
  }

  // 2. Reachability recovery — assert SEMANTIC truth, never the native rect.
  //    (MORPHIC NG Phase 1 fix: the old code clamped GetWindowRect and wrote
  //    the result into bounds_. For a scene-authoritative surface the native
  //    window is a PARKED engine host far offscreen — clamping it dragged the
  //    engine onto the work-area edge and CORRUPTED semantic truth with
  //    parked-space coordinates. Semantic bounds_ are already
  //    reachability-clamped at every write, so recovery means: if the native
  //    placement diverged from semantics (OS shenanigans for native surfaces;
  //    by design for parked engines), re-project semantics through the
  //    projection seam and let the active backend decide native placement.)
  auto bounds_it = bounds_.find(active_);
  if (bounds_it != bounds_.end()) {
    const ClampResult rec = ClampToWorkArea(
        bounds_it->second, ClampPolicy::Reachable,
        ClampReason::ActivationRecovery, ClampPhase::Recovery);
    if (rec.applied) {  // ~never: semantic is pre-clamped at every SetBounds
      LogClamp(IdOf(active_), rec);
      bounds_it->second = rec.rect;
    }
    RECT live{};
    const bool have_live = GetWindowRect(handle, &live) != 0;
    const RECT& semantic = bounds_it->second;
    const bool diverged = !have_live || live.left != semantic.left ||
                          live.top != semantic.top ||
                          live.right != semantic.right ||
                          live.bottom != semantic.bottom;
    if (rec.applied || diverged) {
      if (compositor_) {
        compositor_->Project(active_, semantic);
      } else {
        active_->ApplyGeometry(semantic);
      }
    }
  }

  // 3. Guarded foreground: only when it actually differs (avoids activation loop
  //    on the normal click path where the window is already foreground).
  if (GetForegroundWindow() != handle) {
    DumpNativeZ("activation PRE-foreground", z_order_);   // PHASE 10.5c
    SetForegroundWindow(handle);
    DumpNativeZ("activation POST-foreground", z_order_);  // PHASE 10.5c
  }
}

void SurfaceModel::BeginTransaction() {
  txn_active_ = true;
  z_dirty_ = false;
  activation_dirty_ = false;
  deferred_events_.clear();
  forensic::Log("TXN", "BeginTransaction (freeze z + activation + events)");
}

void SurfaceModel::CommitTransaction() {
  // THE single reconciliation point: apply everything that changed during the
  // epoch exactly once, in a fixed order.
  if (z_dirty_) {
    forensic::Log("TXN", "commit: reconcile z once");
    ReconcileZOrder();
    z_dirty_ = false;
  }
  if (activation_dirty_) {
    ProjectActivation();
    activation_dirty_ = false;
  }
  // Stop deferring before flushing so re-entrant publishes go straight through.
  txn_active_ = false;
  if (!deferred_events_.empty()) {
    forensic::Log("TXN", "commit: flush " +
                             std::to_string(deferred_events_.size()) +
                             " deferred event(s)");
    for (const auto& entry : deferred_events_) {
      if (bus_) {
        bus_->Publish(entry.first, entry.second);
      }
    }
    deferred_events_.clear();
  }
  forensic::Log("TXN", "CommitTransaction done");
}

void SurfaceModel::PublishOrDefer(RuntimeEvent event, SurfaceShell* surface) {
  if (surface == nullptr) {
    return;
  }
  if (txn_active_) {
    deferred_events_.emplace_back(event, surface);
  } else if (bus_) {
    bus_->Publish(event, surface);
  }
}

void SurfaceModel::ProjectVisibility(SurfaceShell* surface) {
  int command = SW_HIDE;
  switch (visibility_[surface]) {
    case VisibilityState::Hidden: command = SW_HIDE; break;
    case VisibilityState::Shown: command = SW_SHOWNA; break;  // show, no activate
    case VisibilityState::Minimized: command = SW_MINIMIZE; break;
  }
  ShowWindow(surface->GetHandle(), command);
}

namespace {
std::string IdOf(SurfaceShell* s) {
  return s ? s->id() : std::string("<none>");
}
}  // namespace
