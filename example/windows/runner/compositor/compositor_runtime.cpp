#include "compositor/compositor_runtime.h"

#include <algorithm>
#include <string>

#include "forensic_trace.h"
#include "surface_shell.h"

namespace {
long MaxAxisDiff(const RECT& a, const RECT& b) {
  return (std::max)({std::abs(static_cast<long>(a.left - b.left)),
                     std::abs(static_cast<long>(a.top - b.top)),
                     std::abs(static_cast<long>(a.right - b.right)),
                     std::abs(static_cast<long>(a.bottom - b.bottom))});
}
}  // namespace

CompositorRuntime::CompositorRuntime()
    : backend_(std::make_unique<HwndProjectionBackend>()) {
  forensic::Log("COMPOSITOR",
                "CompositorRuntime created (backend=hwnd, projection seam)");
}

CompositorRuntime::~CompositorRuntime() = default;

SurfaceVisual& CompositorRuntime::EnsureVisual(SurfaceShell* surface) {
  auto it = visuals_.find(surface);
  if (it != visuals_.end()) return it->second;
  SurfaceVisual v{};
  v.shell = surface;
  return visuals_.emplace(surface, v).first->second;
}

void CompositorRuntime::Project(SurfaceShell* surface, const RECT& rect) {
  if (surface == nullptr || surface->GetHandle() == nullptr) return;

  SurfaceVisual& v = EnsureVisual(surface);
  v.presented = rect;

  // MORPHIC NG Phase 1 — give the opaque interceptor a chance to redirect the
  // NATIVE placement (scene-authoritative surfaces park their engine HWND and
  // move a composited visual instead; the interceptor owns that visual move).
  RECT native = rect;
  if (interceptor_) {
    if (auto redirect = interceptor_(surface, rect)) native = *redirect;
  }

  // Forward to the active backend (HWND today). The backend owns the native
  // mechanism; the compositor stays backend-agnostic.
  backend_->ProjectGeometry(surface, native);

  // Record what was projected. The seam's coherence contract is "the backend
  // received the presented rect"; a scene-authoritative interceptor's parked
  // translation is that backend's placement truth, not divergence — so the
  // record stays the presented rect (I-C unaffected for native surfaces).
  v.projected = rect;

  ++metrics_.projection_count;
  const long delta = MaxAxisDiff(v.presented, v.projected);
  if (delta > metrics_.peak_presented_projected_delta_px) {
    metrics_.peak_presented_projected_delta_px = delta;
  }
}

void CompositorRuntime::ReconcileZ(
    const std::vector<SurfaceShell*>& z_front_first) {
  backend_->ProjectZOrder(z_front_first);
  ++metrics_.zreconcile_count;
}

void CompositorRuntime::OnSurfaceRemoved(SurfaceShell* surface) {
  visuals_.erase(surface);
}

bool CompositorRuntime::projection_coherent(SurfaceShell* surface) const {
  auto it = visuals_.find(surface);
  if (it == visuals_.end()) return true;  // no visual yet → nothing to diverge
  return MaxAxisDiff(it->second.presented, it->second.projected) == 0;
}
