#include "workspace/composition_projector.h"

#include "forensic_trace.h"
#include "surface_manager.h"
#include "surface_model.h"
#include "surface_shell.h"
#include "workspace/workspace_composition_graph.h"

namespace morphic::workspace {

CompositionProjector::CompositionProjector(SurfaceModel* model,
                                           SurfaceManager* manager, EventBus* bus,
                                           WorkspaceCompositionGraph* graph)
    : model_(model), manager_(manager), bus_(bus), graph_(graph) {
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent e, SurfaceShell* s) { OnEvent(e, s); });
  }
  forensic::Log("COMPOSITION", "CompositionProjector created (shared-drag movement)");
}

CompositionProjector::~CompositionProjector() {
  if (bus_ && bus_token_ != 0) {
    bus_->Unsubscribe(bus_token_);
    bus_token_ = 0;
  }
}

void CompositionProjector::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  if constexpr (!kEnableCompositionMovement) {
    return;
  }
  if (projecting_ || surface == nullptr || graph_ == nullptr) return;
  const std::string id = surface->id();
  switch (event) {
    case RuntimeEvent::InteractionBegan:
      // Only a plane ROOT carries others. Snapshot member offsets at drag start.
      if (graph_->IsRoot(id)) {
        active_root_ = id;
        CaptureOffsets(id);
      }
      break;
    case RuntimeEvent::InteractionUpdated:
      if (id == active_root_) Reproject(id);
      break;
    case RuntimeEvent::InteractionEnded:
      if (id == active_root_) {
        Reproject(id);  // final landing
        active_root_.clear();
        offsets_.clear();
      }
      break;
    default:
      break;
  }
}

void CompositionProjector::CaptureOffsets(const std::string& root_id) {
  offsets_.clear();
  const CompositionPlane* plane = graph_->PlaneOfRoot(root_id);
  if (plane == nullptr || !plane->active || model_ == nullptr ||
      manager_ == nullptr) {
    return;
  }
  SurfaceShell* root = manager_->FindById(root_id);
  if (root == nullptr) return;
  const RECT r = model_->bounds(root);
  for (const auto& m : plane->members) {
    SurfaceShell* ms = manager_->FindById(m.surface_id);
    if (ms == nullptr) continue;
    const RECT mb = model_->bounds(ms);
    offsets_[m.surface_id] = POINT{mb.left - r.left, mb.top - r.top};
  }
  forensic::Log("COMPOSITION", "drag begin: captured offsets root=" + root_id +
                                   " members=" + std::to_string(offsets_.size()));
}

void CompositionProjector::Reproject(const std::string& root_id) {
  const CompositionPlane* plane = graph_->PlaneOfRoot(root_id);
  if (plane == nullptr || !plane->active || model_ == nullptr ||
      manager_ == nullptr) {
    return;
  }
  SurfaceShell* root = manager_->FindById(root_id);
  if (root == nullptr) return;
  const RECT r = model_->bounds(root);
  projecting_ = true;
  for (const auto& m : plane->members) {
    auto oit = offsets_.find(m.surface_id);
    if (oit == offsets_.end()) continue;
    SurfaceShell* ms = manager_->FindById(m.surface_id);
    if (ms == nullptr) continue;
    const RECT cur = model_->bounds(ms);
    const LONG w = cur.right - cur.left;
    const LONG h = cur.bottom - cur.top;
    RECT nb{};
    nb.left = r.left + oit->second.x;
    nb.top = r.top + oit->second.y;
    nb.right = nb.left + w;
    nb.bottom = nb.top + h;
    model_->SetBounds(ms, nb);  // routes through clamp + projection seam (members clamped too)
  }
  projecting_ = false;
}

}  // namespace morphic::workspace
