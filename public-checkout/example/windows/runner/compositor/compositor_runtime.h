#ifndef RUNNER_COMPOSITOR_COMPOSITOR_RUNTIME_H_
#define RUNNER_COMPOSITOR_COMPOSITOR_RUNTIME_H_

#include <windows.h>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "compositor/projection_backend.h"
#include "compositor/surface_visual.h"

class SurfaceShell;

// PHASE 9 — CompositorRuntime.
//
// The PROJECTION authority: it owns HOW presented geometry reaches the screen.
// It is the FOURTH and last authority in the stack:
//   semantic (SurfaceModel) → interaction (Epoch) → presentation (Coordinator)
//   → PROJECTION (this) → native (HWND via the backend).
//
// It is a DUMB PROJECTION ROUTER. Deliberately almost boring:
//   - consumes presented rects (read-only — callers hand it the rect to project)
//   - forwards them to the active ProjectionBackend (HWND today)
//   - mirrors semantic z-order onto the backend (never DECIDES z — SurfaceModel does)
//   - records per-surface projected state for divergence telemetry
//
// It has NO policy: no animation, interpolation, topology interpretation, z
// heuristics, settle heuristics, or timing ownership. Those stay in
// PresentationCoordinator / SurfaceModel / InteractionEpoch. It holds NO mutator
// handles to model/graph/session/epoch — structurally incapable of corrupting
// semantic authority (I-C1), exactly like the 8E coordinator.
//
// kCompositorEnabled is a compile-time bypass: false routes callers straight to
// ApplyGeometry (instant rollback to pre-9 behavior). Default true. With the HWND
// backend, enabled vs disabled are byte-identical — the seam is transparent.
class CompositorRuntime {
 public:
  static constexpr bool kCompositorEnabled = true;

  CompositorRuntime();
  ~CompositorRuntime();

  CompositorRuntime(const CompositorRuntime&) = delete;
  CompositorRuntime& operator=(const CompositorRuntime&) = delete;

  // Project `rect` onto `surface`'s native presentation via the active backend.
  // Lazily creates the SurfaceVisual record on first touch. Synchronous +
  // immediate (I-C3 — no added latency; the HWND backend is a straight
  // ApplyGeometry). Called by SurfaceModel::SetBounds (direct) and
  // PresentationCoordinator::OnTick (settle).
  void Project(SurfaceShell* surface, const RECT& rect);

  // MORPHIC NG Phase 1 (ProjectionTruth seam) — an OPAQUE per-projection
  // interceptor, same firewall pattern as SurfaceGraph's groupability
  // predicate: the product layer may observe each presented rect and return a
  // NATIVE placement override (e.g. the parked-engine translation for
  // scene-authoritative surfaces, moving their composited visual itself).
  // The compositor learns nothing about backends/kinds/scene state; nullopt =
  // project the presented rect unchanged. Null interceptor = byte-identical
  // pre-NG behavior.
  using ProjectionInterceptor =
      std::function<std::optional<RECT>(SurfaceShell*, const RECT&)>;
  void SetProjectionInterceptor(ProjectionInterceptor interceptor) {
    interceptor_ = std::move(interceptor);
  }

  // Mirror semantic z-order (front == index 0) onto the backend. Called by
  // SurfaceModel::ReconcileZOrder.
  void ReconcileZ(const std::vector<SurfaceShell*>& z_front_first);

  // Lifecycle: drop a surface's visual when it is destroyed (called from the
  // runtime's bus subscription). Surfaces are added lazily by Project.
  void OnSurfaceRemoved(SurfaceShell* surface);

  // Telemetry (read by the harness/auditor; cheap).
  struct Metrics {
    long long projection_count = 0;
    long long zreconcile_count = 0;
    long peak_presented_projected_delta_px = 0;  // 0 with HWND backend
  };
  const Metrics& metrics() const { return metrics_; }
  size_t visual_count() const { return visuals_.size(); }

  // True iff `surface` has a visual whose projected == its last presented (no
  // backend divergence). Used by the IntegrityAuditor (I-C: presented==projected).
  bool projection_coherent(SurfaceShell* surface) const;

 private:
  SurfaceVisual& EnsureVisual(SurfaceShell* surface);

  std::unique_ptr<ProjectionBackend> backend_;
  std::unordered_map<SurfaceShell*, SurfaceVisual> visuals_;
  Metrics metrics_{};
  ProjectionInterceptor interceptor_;  // NG Phase 1 — opaque, default null
};

#endif  // RUNNER_COMPOSITOR_COMPOSITOR_RUNTIME_H_
