#include "surface_policy/activation_policy.h"

#include "surface_policy/surface_policy.h"

namespace morphic::policy {

std::vector<std::string> ActivationClusterFor(
    const std::string& root_id, const SurfaceRegistry& registry,
    const workspace::WorkspaceManager& workspaces) {
  std::vector<std::string> cluster;

  const SurfaceDescriptor* root = registry.Get(root_id);
  if (root == nullptr) return cluster;

  // Global surfaces (launcher) + transient overlays form NO cluster — activating
  // them raises only themselves.
  if (SurfacePolicy::IsGlobal(root->kind) ||
      root->kind == SurfaceKind::Overlay ||
      root->kind == SurfaceKind::Command) {
    cluster.push_back(root_id);
    return cluster;
  }

  // Resolve the workspace this root belongs to. If the root is itself an owned
  // utility, anchor the cluster on its workspace; the cluster is the workspace's
  // members minus overlays/global.
  const std::string ws = root->workspace_id.empty()
                             ? workspaces.WorkspaceOf(root_id)
                             : root->workspace_id;

  cluster.push_back(root_id);
  if (ws.empty()) return cluster;  // detached/no-workspace root → solo

  for (const auto& sid : workspaces.SurfacesOf(ws)) {
    if (sid == root_id) continue;
    const SurfaceDescriptor* d = registry.Get(sid);
    if (d == nullptr) continue;
    // Cluster members: same-workspace owned utilities (palette/inspector/utility).
    // EXCLUDE overlays (transient), global, and other workspace roots (a second
    // Workspace surface in the same ws is its own root — rare today, but exclude).
    if (d->kind == SurfaceKind::Overlay || d->kind == SurfaceKind::Command) continue;
    if (SurfacePolicy::IsGlobal(d->kind)) continue;
    if (d->kind == SurfaceKind::Workspace ||
        d->kind == SurfaceKind::DetachedWorkspace) {
      continue;
    }
    cluster.push_back(sid);
  }
  return cluster;
}

}  // namespace morphic::policy
