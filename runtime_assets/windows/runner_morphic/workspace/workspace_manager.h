#ifndef RUNNER_WORKSPACE_WORKSPACE_MANAGER_H_
#define RUNNER_WORKSPACE_WORKSPACE_MANAGER_H_

#include <string>
#include <unordered_map>
#include <vector>

#include "workspace/workspace_model.h"

// PHASE 10E — WorkspaceManager.
//
// Owns workspace identity + surface membership. Product layer; the runtime never
// sees it. Pure ownership bookkeeping — NO geometry, z, interaction, or
// presentation. Surfaces are referenced by opaque string ids.
namespace morphic::workspace {

class WorkspaceManager {
 public:
  WorkspaceManager();

  // Create a workspace; returns its generated id. The first workspace created
  // becomes Current() if none is set.
  std::string Create(const std::string& title);

  // PHASE 10.2 — destroy a workspace. Returns the surface ids that were in it
  // (the caller is responsible for destroying them). Removes the workspace from
  // the map. If the destroyed workspace was Current(), picks another or clears.
  std::vector<std::string> Destroy(const std::string& workspace_id);

  // Membership ops (id-based; no-ops if the workspace/surface is absent).
  void AddSurface(const std::string& workspace_id, const std::string& surface_id);
  void RemoveSurface(const std::string& surface_id);  // remove from whatever workspace owns it

  // Queries.
  const Workspace* Get(const std::string& workspace_id) const;
  std::vector<std::string> SurfacesOf(const std::string& workspace_id) const;
  // The workspace owning `surface_id`, or "" if none.
  std::string WorkspaceOf(const std::string& surface_id) const;
  std::vector<Workspace> All() const;
  size_t size() const { return workspaces_.size(); }

  // Current (active) workspace — the one new surfaces default into.
  const std::string& Current() const { return current_; }
  void SetCurrent(const std::string& workspace_id);

 private:
  std::unordered_map<std::string, Workspace> workspaces_;
  std::string current_;
  unsigned next_id_ = 1;
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_WORKSPACE_MANAGER_H_
