#ifndef RUNNER_WORKSPACE_WORKSPACE_COMPOSITION_GRAPH_H_
#define RUNNER_WORKSPACE_WORKSPACE_COMPOSITION_GRAPH_H_

#include <string>
#include <unordered_map>

#include "runtime_events.h"  // EventBus + Token (runtime header — arrow points DOWN, allowed)
#include "workspace/composition_plane.h"

class EventBus;
class SurfaceShell;

// PHASE 12B — WorkspaceCompositionGraph.
//
// The store of composition planes (product layer). DELIBERATELY SEPARATE from
// SurfaceGraph (topology) — composition is projection-only, never grouping/docking/
// fracture. Keyed by root id (one plane per root). Auto-cleans on SurfaceDestroyed
// (keyed by string id, like SurfaceRegistry). ≤1 plane per surface, no nesting (Q8).
namespace morphic::workspace {

class WorkspaceCompositionGraph {
 public:
  explicit WorkspaceCompositionGraph(EventBus* bus);
  ~WorkspaceCompositionGraph();

  WorkspaceCompositionGraph(const WorkspaceCompositionGraph&) = delete;
  WorkspaceCompositionGraph& operator=(const WorkspaceCompositionGraph&) = delete;

  // Add `member` to `root`'s plane (created on first member). Rejected (no-op + WARN) if
  // the member is already a root or already a member, or the root is itself a member —
  // enforcing ≤1 plane / no nesting.
  void AddMember(const std::string& root_id, const std::string& member_id,
                 morphic::policy::MemberDragBehavior drag);

  // Remove a surface: a MEMBER is dropped from its plane (plane dissolves if it empties);
  // a ROOT dissolves the whole plane.
  void RemoveSurface(const std::string& surface_id);

  bool IsRoot(const std::string& id) const;
  std::string RootOfMember(const std::string& member_id) const;  // "" if not a member
  const CompositionPlane* PlaneOfRoot(const std::string& root_id) const;
  size_t plane_count() const { return planes_.size(); }

 private:
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);

  EventBus* bus_;  // not owned (runtime-owned)
  EventBus::Token bus_token_ = 0;
  std::unordered_map<std::string, CompositionPlane> planes_;     // root_id -> plane
  std::unordered_map<std::string, std::string> member_to_root_;  // member_id -> root_id
  unsigned next_id_ = 1;
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_WORKSPACE_COMPOSITION_GRAPH_H_
