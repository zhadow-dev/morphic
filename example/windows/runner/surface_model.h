#ifndef RUNNER_SURFACE_MODEL_H_
#define RUNNER_SURFACE_MODEL_H_

#include <windows.h>

#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "runtime_events.h"  // RuntimeEvent

class SurfaceShell;
class EventBus;
class CompositorRuntime;

// PHASE: stabilization (CORE_HARDENING §5 — authority separation).
//
// The SEMANTIC source of truth for surface state, kept INDEPENDENT of native
// OS state. Native HWND z-order, foreground, and visibility are now
// PROJECTIONS of this model — not the model itself. This is the inversion from
// "OS state IS runtime state" to "runtime state exists; native is a projection."
//
// The five authorities are explicit and SEPARABLE:
//   - semantic Z  : z_order_   (front() == top, pure intent)  → SetWindowPos
//   - native Z    : (the projection above; NOT stored separately)
//   - activation  : active_    (semantically active)          → guarded SetForegroundWindow
//   - focus       : focused_   (keyboard owner)               → recorded only (see Focus())
//   - visibility  : visibility_[s]                            → ShowWindow
//   - geometry    : bounds_[s] (semantic screen rect)         → SetWindowPos (NOZORDER|NOACTIVATE)
//
// NO tiers yet — z is a flat vector. The point of this phase is the SEPARATION
// of concepts, not richer policy.
//
// PHASE 7A — GEOMETRY AUTHORITY: native HWND position/size is now a PROJECTION
// of bounds_. The InteractionRouter is the sole MUTATOR (drag/resize compute
// new RECTs and call SetBounds); SurfaceModel is the sole PROJECTOR (one
// SetWindowPos path). Scattered SetWindowPos/MoveWindow on surface bounds is
// forbidden (LayoutChild remains valid — it lays out the CHILD into our client
// area, not surface geometry).
enum class VisibilityState { Hidden, Shown, Minimized };

class SurfaceModel {
 public:
  void SetEventBus(EventBus* bus) { bus_ = bus; }
  // PHASE 9 — projection seam. When set, geometry + z projection route through the
  // CompositorRuntime (which forwards to the active backend = HWND today) instead
  // of calling ApplyGeometry / SetWindowPos directly. Null = direct (rollback).
  void SetCompositor(CompositorRuntime* compositor) { compositor_ = compositor; }

  // Membership (driven by SurfaceManager lifecycle).
  void Add(SurfaceShell* surface);     // enters on top, Hidden
  void Remove(SurfaceShell* surface);  // also clears active_/focused_ if it

  // The four SEPARATE semantic operations. The router composes them for the
  // current "click → activate + raise + focus" policy, but each is
  // independently callable (e.g. a future overlay could Raise without Activate;
  // a palette could Focus without Raise).
  void Raise(SurfaceShell* surface);     // semantic z only
  void Activate(SurfaceShell* surface);  // semantic activation only
  void Focus(SurfaceShell* surface);     // semantic keyboard focus only
  void Show(SurfaceShell* surface);      // visibility → Shown

  void SetVisibility(SurfaceShell* surface, VisibilityState vis);

  // M2.3E — NATIVE WINDOW CONTROLS, hybrid authority. Minimize is a VISIBILITY state (native:
  // ShowWindow); maximize is a GEOMETRY transition (SEMANTIC: save the current rect, SetBounds to
  // the monitor work area). We never call SW_MAXIMIZE — so the compositor keeps projecting model
  // bounds unchanged (no un-maximize, no auditor divergence, no taskbar-cover). Restore un-minimizes
  // and/or returns to the saved rect. The model is the single geometry authority; the OS owns
  // visibility + input. Each fires on_window_state_ so the runtime can project state to content.
  void Minimize(SurfaceShell* surface);
  void Maximize(SurfaceShell* surface);
  void Restore(SurfaceShell* surface);
  void ToggleMaximize(SurfaceShell* surface);
  // M2.3E polish — restore-on-drag: un-maximize and reposition the restored rect UNDER the cursor
  // (Windows behavior — grab a maximized window's titlebar and it pops back to size beneath the
  // pointer). Called from the drag-begin path when the surface is maximized; no-op otherwise.
  void RestoreUnderCursor(SurfaceShell* surface, POINT screen);
  // NOTE: NOT IsMaximized/IsMinimized — those are Win32 macros (winuser.h) that expand to
  // IsZoomed/IsIconic and silently mangle the method name across translation units.
  bool IsSurfaceMaximized(SurfaceShell* surface) const;
  bool IsSurfaceMinimized(SurfaceShell* surface) const;
  // Sync point when the OS restored a window we held semantically minimized (taskbar click →
  // WM_SIZE/SIZE_RESTORED); keeps visibility_ honest and re-projects window state.
  void NoteNativeRestored(SurfaceShell* surface);
  // Fired after every window-state transition (max/restore/minimize/native-restore) with the
  // affected surface, so the runtime can push {maximized,minimized} to that surface's content.
  void SetOnWindowStateChanged(std::function<void(SurfaceShell*)> cb) {
    on_window_state_ = std::move(cb);
  }

  // PHASE 7A — geometry authority. The SINGLE choke point for surface bounds.
  // Mutates semantic bounds and immediately projects onto native HWND geometry
  // (SetWindowPos with SWP_NOZORDER|SWP_NOACTIVATE so geometry NEVER perturbs
  // z-order or activation, even mid-interaction). Geometry projection is NOT
  // deferred by the interaction transaction — only z and activation are. Live
  // bounds projection is what keeps the message pump unblocked and Flutter
  // rendering during runtime-owned drag/resize.
  void SetBounds(SurfaceShell* surface, const RECT& bounds);
  RECT bounds(SurfaceShell* surface) const;

  // PHASE 8E — set SEMANTIC bounds WITHOUT projecting to the native HWND. Same
  // clamp + single-writer guarantees as SetBounds, but it does NOT call
  // ApplyGeometry — the caller (PresentationCoordinator) owns the native
  // projection while it eases presented → semantic during a settle. Returns the
  // CLAMPED semantic rect actually stored (so the coordinator knows the true
  // target). This is the three-layer seam: semantic truth here, native projection
  // deferred to the coordinator. Used ONLY by the settle path; the direct
  // drag/resize path still uses SetBounds (immediate native, 1:1).
  RECT SetSemanticBounds(SurfaceShell* surface, const RECT& bounds);

  // Re-project the entire semantic z-order onto native HWND order.
  void ReconcileZOrder();

  // Interaction transaction (CORE_HARDENING §6) — temporal authority. Between
  // Begin and Commit, z-order reconciliation and activation projection are
  // FROZEN and state events are BUFFERED; everything is applied ONCE at commit.
  // This converts immediate-mode churn into a single reconciliation point.
  void BeginTransaction();
  void CommitTransaction();
  bool InTransaction() const { return txn_active_; }

  SurfaceShell* active() const { return active_; }
  SurfaceShell* focused() const { return focused_; }
  const std::vector<SurfaceShell*>& z_order() const { return z_order_; }

  // PHASE 7B — count of SetBounds calls dropped because we were already inside
  // a projection (R4 reentrancy guard). Lifetime counter; the router snapshots
  // it at Begin/End to attribute per-interaction reentrancy.
  int reentrant_drops() const { return reentrant_drops_; }

  // PHASE 7C-T — test hook for R4 validation. The callback is invoked AFTER
  // projecting_=true is set but BEFORE SetWindowPos returns, i.e. inside the
  // projection epoch. A FaultInjector wires a callback that re-enters SetBounds
  // so we can confirm the guard actually drops + counts the inner call rather
  // than just hoping nothing currently re-enters. Production: leave unset.
  using TestProjectionCallback = std::function<void()>;
  void SetTestProjectionCallback(TestProjectionCallback cb) {
    test_projection_callback_ = std::move(cb);
  }

 private:
  void ProjectActivation();                  // semantic active_ → native foreground
  void ProjectVisibility(SurfaceShell* surface);  // visibility_ → ShowWindow
  // Publish now, or buffer until commit if a transaction is open.
  void PublishOrDefer(RuntimeEvent event, SurfaceShell* surface);

  EventBus* bus_ = nullptr;  // not owned
  CompositorRuntime* compositor_ = nullptr;  // PHASE 9 — projection seam (not owned)
  std::vector<SurfaceShell*> z_order_;  // front() == topmost (semantic intent)
  SurfaceShell* active_ = nullptr;
  SurfaceShell* focused_ = nullptr;
  std::unordered_map<SurfaceShell*, VisibilityState> visibility_;
  std::unordered_map<SurfaceShell*, RECT> bounds_;  // semantic screen rect

  // M2.3E — window-state authority (semantic). Per-surface pre-maximize rect + maximized flag.
  std::unordered_map<SurfaceShell*, RECT> restore_bounds_;
  std::unordered_map<SurfaceShell*, bool> maximized_;
  std::function<void(SurfaceShell*)> on_window_state_;  // state-change projection hook

  // PHASE 7B — reentrancy guard for the geometry projection path.
  // SetWindowPos synchronously dispatches WM_WINDOWPOSCHANGED/MOVE/SIZE to the
  // shell's WndProc. If anything down that stack ever calls back into SetBounds
  // (today nothing does, but the call graph is open), the inner call is dropped
  // and counted instead of corrupting bounds_ or recursing. This addresses 7A
  // risk R4 by closing the loop structurally.
  bool projecting_ = false;
  int reentrant_drops_ = 0;
  TestProjectionCallback test_projection_callback_;

  // PHASE 8B-prep — reentrancy guards for semantic-state mutators (I-A4).
  // SetBounds already had this; Activate/Raise/Focus did not. Each of these
  // publishes a RuntimeEvent at the end, and observers can re-enter the model
  // from those publishes (especially likely once grouped interactions land).
  // Any nested call to the same method while the outer is on-stack is dropped
  // and counted; the dedup in each method (e.g. `if (active_ == surface)`)
  // doesn't help when the nested call targets a DIFFERENT surface.
  bool activating_ = false;
  bool raising_ = false;
  bool focusing_ = false;

  // Transaction state.
  bool txn_active_ = false;
  bool z_dirty_ = false;           // semantic z changed during the transaction
  bool activation_dirty_ = false;  // active_ changed during the transaction
  std::vector<std::pair<RuntimeEvent, SurfaceShell*>> deferred_events_;
};

#endif  // RUNNER_SURFACE_MODEL_H_
