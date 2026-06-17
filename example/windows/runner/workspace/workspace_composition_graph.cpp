#include "workspace/workspace_composition_graph.h"

#include <algorithm>

#include "forensic_trace.h"
#include "surface_shell.h"

namespace morphic::workspace {

WorkspaceCompositionGraph::WorkspaceCompositionGraph(EventBus* bus) : bus_(bus) {
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent e, SurfaceShell* s) { OnEvent(e, s); });
  }
  forensic::Log("COMPOSITION", "WorkspaceCompositionGraph created");
}

WorkspaceCompositionGraph::~WorkspaceCompositionGraph() {
  if (bus_ && bus_token_ != 0) {
    bus_->Unsubscribe(bus_token_);
    bus_token_ = 0;
  }
}

void WorkspaceCompositionGraph::AddMember(const std::string& root_id,
                                          const std::string& member_id,
                                          morphic::policy::MemberDragBehavior drag) {
  if (root_id.empty() || member_id.empty() || root_id == member_id) return;
  // No nesting / ≤1 plane: a member can't be a root or already a member, and a root
  // can't itself be someone else's member.
  if (planes_.count(member_id) || member_to_root_.count(member_id)) {
    forensic::Log("COMPOSITION WARN",
                  "AddMember rejected (already in a plane) member=" + member_id);
    return;
  }
  if (member_to_root_.count(root_id)) {
    forensic::Log("COMPOSITION WARN",
                  "AddMember rejected (root is itself a member) root=" + root_id);
    return;
  }
  CompositionPlane& plane = planes_[root_id];
  if (plane.plane_id.empty()) {
    plane.plane_id = "plane" + std::to_string(next_id_++);
    plane.root_surface_id = root_id;
  }
  plane.members.push_back(CompositionMember{member_id, drag});
  member_to_root_[member_id] = root_id;
  forensic::Log("COMPOSITION",
                "AddMember plane=" + plane.plane_id + " root=" + root_id +
                    " member=" + member_id + " (" +
                    std::string(morphic::policy::ToString(drag)) + ")");
}

void WorkspaceCompositionGraph::RemoveSurface(const std::string& id) {
  // Member removal.
  auto mit = member_to_root_.find(id);
  if (mit != member_to_root_.end()) {
    const std::string root = mit->second;
    member_to_root_.erase(mit);
    auto pit = planes_.find(root);
    if (pit != planes_.end()) {
      auto& mem = pit->second.members;
      mem.erase(std::remove_if(mem.begin(), mem.end(),
                               [&id](const CompositionMember& m) {
                                 return m.surface_id == id;
                               }),
                mem.end());
      forensic::Log("COMPOSITION", "RemoveSurface member=" + id +
                                       " from plane=" + pit->second.plane_id);
      if (mem.empty()) {
        forensic::Log("COMPOSITION",
                      "plane dissolved (empty) " + pit->second.plane_id);
        planes_.erase(pit);
      }
    }
    return;
  }
  // Root removal → dissolve the whole plane.
  auto pit = planes_.find(id);
  if (pit != planes_.end()) {
    for (const auto& m : pit->second.members) member_to_root_.erase(m.surface_id);
    forensic::Log("COMPOSITION",
                  "RemoveSurface root=" + id + " dissolved plane=" +
                      pit->second.plane_id);
    planes_.erase(pit);
  }
}

bool WorkspaceCompositionGraph::IsRoot(const std::string& id) const {
  return planes_.count(id) > 0;
}

std::string WorkspaceCompositionGraph::RootOfMember(
    const std::string& member_id) const {
  auto it = member_to_root_.find(member_id);
  return it == member_to_root_.end() ? std::string() : it->second;
}

const CompositionPlane* WorkspaceCompositionGraph::PlaneOfRoot(
    const std::string& root_id) const {
  auto it = planes_.find(root_id);
  return it == planes_.end() ? nullptr : &it->second;
}

void WorkspaceCompositionGraph::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  // Auto-clean on destroy (read the id before the pointer dies; we never retain it).
  if (event == RuntimeEvent::SurfaceDestroyed && surface != nullptr) {
    RemoveSurface(surface->id());
  }
}

}  // namespace morphic::workspace
