#ifndef RUNNER_SURFACE_POLICY_ACTIVATION_POLICY_H_
#define RUNNER_SURFACE_POLICY_ACTIVATION_POLICY_H_

#include <string>
#include <vector>

#include "surface_policy/surface_registry.h"
#include "workspace/workspace_manager.h"

// PHASE 10.3E — ActivationPolicy.
//
// PURE policy: given a root surface id, compute the "activation cluster" — the set
// of surfaces that semantically belong together for activation (a workspace root +
// its owned palettes/inspectors), EXCLUDING overlays, the global launcher, and
// unrelated workspaces.
//
// IMPORTANT (restraint): this function MUTATES NOTHING — it returns ids. Win32
// OWNER CHAINS (set at creation in 10.3D) already make owned windows raise WITH
// their owner natively, so the cluster is mostly enforced by the OS for free. This
// computation is the SEMANTIC truth (for logging/verification, and future
// activation work) — we do NOT invent a runtime activation API to "raise a
// cluster" this pass. Observe behavior first; automate later only if needed.
namespace morphic::policy {

std::vector<std::string> ActivationClusterFor(
    const std::string& root_id, const SurfaceRegistry& registry,
    const workspace::WorkspaceManager& workspaces);

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_ACTIVATION_POLICY_H_
