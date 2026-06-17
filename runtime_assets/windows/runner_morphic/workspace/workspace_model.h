#ifndef RUNNER_WORKSPACE_WORKSPACE_MODEL_H_
#define RUNNER_WORKSPACE_WORKSPACE_MODEL_H_

#include <string>
#include <vector>

// PHASE 10E — Workspace.
//
// Morphic had surfaces; it now has WORKSPACE IDENTITY. A Workspace is an ownership
// grouping of surfaces — the unit a user inhabits and (eventually) the unit that
// persists/restores. This is OWNERSHIP semantics ONLY: no geometry, z-order, or
// interaction (those stay in the runtime, which knows nothing about workspaces).
//
// Product layer — the runtime never sees this. Surfaces are referenced by their
// opaque string ids (the join key shared with the runtime + the registry).
namespace morphic::workspace {

struct Workspace {
  std::string id;
  std::string title;
  std::vector<std::string> surface_ids;  // membership (ordered: creation order)
  bool persistent = true;                // (consumed by 10F; durable by default)
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_WORKSPACE_MODEL_H_
