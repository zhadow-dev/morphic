#include "workspace/plane_activation_model.h"

#include "forensic_trace.h"
#include "surface_shell.h"
#include "workspace/workspace_composition_graph.h"

namespace morphic::workspace {

PlaneActivationModel::PlaneActivationModel(WorkspaceCompositionGraph* graph, EventBus* bus)
    : graph_(graph), bus_(bus) {
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent e, SurfaceShell* s) { OnEvent(e, s); });
  }
  forensic::Log("PLANE", "PlaneActivationModel created (Stage 1 — semantic activation only)");
}

PlaneActivationModel::~PlaneActivationModel() {
  if (bus_ && bus_token_ != 0) {
    bus_->Unsubscribe(bus_token_);
    bus_token_ = 0;
  }
}

std::string PlaneActivationModel::PlaneRootOf(const std::string& surface_id) const {
  if (graph_ == nullptr) return "";
  // A surface participates in a plane either as its root or as a member of one.
  if (graph_->IsRoot(surface_id)) return surface_id;
  return graph_->RootOfMember(surface_id);  // "" when not a member
}

bool PlaneActivationModel::IsSurfaceInActivePlane(const std::string& surface_id) const {
  if (active_root_.empty()) return false;
  return PlaneRootOf(surface_id) == active_root_;
}

bool PlaneActivationModel::IsPlaneMemberSurface(const std::string& surface_id) const {
  if (graph_ == nullptr) return false;
  return !graph_->RootOfMember(surface_id).empty();  // member iff it has a root
}

bool PlaneActivationModel::IsSurfaceVisuallyActive(const std::string& surface_id) const {
  if (active_root_.empty()) {
    // No active plane → only the active free surface itself reads active.
    return surface_id == leader_surface_id_;
  }
  return PlaneRootOf(surface_id) == active_root_;
}

void PlaneActivationModel::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  if constexpr (!kEnablePlaneActivation) {
    return;
  }
  // Both events mean "this surface just became the user's focus". RequestActivate composes
  // Activate + Raise + Focus, so they typically fire together — the transition guard below
  // collapses the duplicate into a single [PLANE] line.
  if (event != RuntimeEvent::SurfaceActivated &&
      event != RuntimeEvent::SurfaceFocused) {
    return;
  }
  if (surface == nullptr) return;

  const std::string id = surface->id();
  const std::string root = PlaneRootOf(id);  // "" = free surface (no plane)

  // Transition guard: only act when the (active plane, leader) pair actually changes.
  if (root == active_root_ && id == leader_surface_id_) return;

  active_root_ = root;
  leader_surface_id_ = id;

  if (active_root_.empty()) {
    // A free / floating surface is active — no plane reads active. (This is correct, not a
    // failure: a single unattached surface owns activation by itself.)
    forensic::Log("PLANE", "active=<none> leader=" + id + " (free surface)");
  } else {
    // Resolve the stable plane id for the trace (root id is the key; plane_id is identity).
    std::string plane_id = active_root_;
    if (const CompositionPlane* plane = graph_->PlaneOfRoot(active_root_)) {
      plane_id = plane->plane_id;
    }
    forensic::Log("PLANE", "active=" + plane_id + " root=" + active_root_ +
                               " leader=" + id);
  }

  // Stage 2A — notify the visual projector AFTER state + log are final (ordering-safe).
  if (on_changed_) on_changed_();
}

}  // namespace morphic::workspace
