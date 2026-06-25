#ifndef RUNNER_SURFACE_POLICY_SURFACE_KIND_H_
#define RUNNER_SURFACE_POLICY_SURFACE_KIND_H_

// PHASE 10A — SurfaceKind.
//
// PRODUCT-LAYER semantics. This header lives ABOVE the runtime and the runtime
// MUST NEVER include it. SurfaceModel / SurfaceManager / InteractionSession /
// PresentationCoordinator / CompositorRuntime stay surface-type-AGNOSTIC: to them
// a surface is an opaque std::string id. SurfaceKind is the product's behavioral
// vocabulary, consumed only by the surface_policy/ + workspace/ layers.
//
// These are behavioral ROLES, not rendering types. Deliberately SMALL and
// under-specialized: the taxonomy is expected to be partly WRONG and will be tuned
// after real use (which is exactly why persistence (10F) is deferred — we must not
// serialize an unproven taxonomy). Do NOT add 40 kinds or predict future UI
// fantasies; let lived semantics drive what's real.

namespace morphic::policy {

enum class SurfaceKind {
  Workspace,          // a primary inhabitable surface (root of a workspace)
  DetachedWorkspace,  // a workspace surface pulled out to live independently
  ToolPalette,        // a tool/command palette attached to a workspace
  Overlay,            // a transient visual subordinate (never persists)
  Inspector,          // a context panel that follows workspace/parent ownership
  Utility,            // a small auxiliary surface (settings, status, etc.)
  Command,            // a transient command surface (launcher/quick-action)
  // PHASE 10.3F — GLOBAL meta-control surface (the ecology launcher). NOT
  // workspace content: it belongs to no workspace, is never grouped/detached,
  // and is global control infrastructure. Distinct role from Utility (which is
  // workspace-owned). Its exclusions all fall out of policy predicates.
  EcologyLauncher,
};

inline const char* ToString(SurfaceKind k) {
  switch (k) {
    case SurfaceKind::Workspace:         return "workspace";
    case SurfaceKind::DetachedWorkspace: return "detached_workspace";
    case SurfaceKind::ToolPalette:       return "tool_palette";
    case SurfaceKind::Overlay:           return "overlay";
    case SurfaceKind::Inspector:         return "inspector";
    case SurfaceKind::Utility:           return "utility";
    case SurfaceKind::Command:           return "command";
    case SurfaceKind::EcologyLauncher:   return "ecology_launcher";
  }
  return "?";
}

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_SURFACE_KIND_H_
