#ifndef RUNNER_PRESENTATION_PRESENTATION_COORDINATOR_H_
#define RUNNER_PRESENTATION_PRESENTATION_COORDINATOR_H_

#include <windows.h>

#include <unordered_map>

#include "frame_clock.h"

class SurfaceModel;
class SurfaceShell;
class CompositorRuntime;

// PHASE 8E — PresentationCoordinator.
//
// The presentation layer. Establishes the three-layer separation:
//
//   Semantic   = model.bounds(surface)   — truth, changes INSTANTLY (SurfaceModel)
//   Presented  = presented_[surface]     — approaches semantic (THIS class)
//   Native     = the HWND rect           — what ApplyGeometry projected
//
// SACRED BOUNDARY (the entire point of 8E): this class consumes READ-ONLY semantic
// truth and projects presentation onto the native HWND. It is STRUCTURALLY
// incapable of mutating semantics — it holds ONLY a SurfaceModel* (read bounds) +
// a FrameClock* (tick). It has NO handle to the graph, sessions, epochs, or
// z-order, so it cannot write topology / interaction / activation even by mistake.
// Projection only. (I-E1 enforced by construction, not convention.)
//
// SCOPE (deliberately tiny — NOT a motion engine): the ONLY thing it smooths today
// is the SETTLE case — when an interaction ENDS and the final semantic position
// differs from the last presented (clamp correction at release, snap, coalesce
// residue), it eases presented → semantic over a few ticks instead of a hard jump.
// Direct manipulation DURING a drag/resize is NEVER routed here (it stays strictly
// 1:1 via SurfaceModel::SetBounds — the 7E-rev3 lesson). There are NO springs,
// inertia, easing catalogs, transition types, or per-surface motion policies. If a
// future change tempts this class to grow general motion infrastructure, refuse it.
//
// Lazy-idle: subscribes to the clock on the FIRST active settle, unsubscribes when
// all settles complete — zero idle cost (presentation is dormant by design today).
class PresentationCoordinator {
 public:
  // Critically-damped approach time constant. ~60ms → presented closes ~63% of the
  // gap per tau; with 16ms ticks that's ~3-4 ticks to settle. Small + boring.
  static constexpr double kSettleTauMs = 60.0;
  static constexpr long kSettleSnapPx = 1;       // within this → snap done
  static constexpr int kMaxSettleTicks = 30;     // watchdog: force-complete after N
  // Compile-time kill switch. false = hard snap (no interpolation) — instant
  // rollback to pre-8E behavior. Default true.
  static constexpr bool kPresentationEnabled = true;

  PresentationCoordinator(SurfaceModel* model, FrameClock* clock);
  ~PresentationCoordinator();

  // PHASE 9 — the coordinator hands its eased (presented) rect to the projection
  // seam instead of calling ApplyGeometry directly. Set once at wiring. Null =
  // direct ApplyGeometry (rollback / pre-wiring). The coordinator stays the
  // presentation authority; the compositor only owns HOW the rect reaches screen.
  void SetCompositor(CompositorRuntime* compositor) { compositor_ = compositor; }

  PresentationCoordinator(const PresentationCoordinator&) = delete;
  PresentationCoordinator& operator=(const PresentationCoordinator&) = delete;

  // Begin a settle for `surface`: ease the native projection from `from` to the
  // surface's CURRENT semantic bounds over a few ticks. Called ONLY at interaction
  // end, ONLY when there is a residual gap worth smoothing. If presentation is
  // disabled (or surface/clock null), this is a no-op and the caller should have
  // already hard-projected. `from` is the last presented position.
  void RequestSettle(SurfaceShell* surface, const RECT& from);

  // Drop any in-flight settle for a surface (e.g. on destroy, or when a new
  // interaction begins on it — direct manipulation supersedes a settle).
  void CancelSettle(SurfaceShell* surface);

  // Telemetry surface (read by the harness; cheap).
  struct Metrics {
    int settle_count = 0;            // settles started
    int settle_completed = 0;        // settles that snapped to semantic
    int settle_forced = 0;           // watchdog force-completions (should be 0)
    long peak_delta_px = 0;          // peak presented↔semantic gap observed
    double peak_settle_ms = 0.0;     // longest settle duration
  };
  const Metrics& metrics() const { return metrics_; }
  size_t active_settles() const { return settles_.size(); }

  // Read the current presented rect (== semantic when no settle in flight).
  RECT presented(SurfaceShell* surface) const;

 private:
  struct Settle {
    RECT presented{};            // current presentation position (eased)
    LARGE_INTEGER start_qpc{};   // for settle duration metric
    int ticks = 0;               // watchdog counter
  };

  void OnTick(double dt_ms);     // advance every active settle
  void StartClockIfNeeded();
  void StopClockIfIdle();
  // PHASE 9 — hand the eased rect to the projection seam (compositor) if wired,
  // else straight to ApplyGeometry. The coordinator owns presented truth; the
  // compositor owns HOW it reaches the screen.
  void ProjectNative(SurfaceShell* surface, const RECT& rect);

  SurfaceModel* model_;          // READ-ONLY (bounds). NOT mutated.
  FrameClock* clock_;            // tick source (lazy-idle subscription).
  CompositorRuntime* compositor_ = nullptr;  // PHASE 9 — projection seam (not owned)
  FrameClock::Token clock_token_ = 0;

  std::unordered_map<SurfaceShell*, Settle> settles_;
  Metrics metrics_{};
};

#endif  // RUNNER_PRESENTATION_PRESENTATION_COORDINATOR_H_
