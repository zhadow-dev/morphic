#ifndef RUNNER_SURFACE_POLICY_NATIVE_SURFACE_BEHAVIOR_H_
#define RUNNER_SURFACE_POLICY_NATIVE_SURFACE_BEHAVIOR_H_

// PHASE 10.3 — NativeSurfaceBehavior.
//
// PURE DATA. The product-layer description of how a surface KIND should behave at
// the native desktop / window-manager level (Alt+Tab, taskbar, ownership,
// transience). This is the intermediate representation between SurfaceKind
// (product) and plain Win32 flags (runtime). The runtime NEVER sees this struct or
// SurfaceKind — the policy layer resolves this and then hands the RUNTIME only
// generic Win32 values (a DWORD ex-style + an owner HWND).
//
// No Win32 includes here on purpose — this is pure semantic intent; the mapping to
// actual WS_EX_* lives in native_surface_policy.cpp.
namespace morphic::policy {

// Coarse shell role (for logging / future grouping decisions).
enum class SurfaceShellBehavior {
  PrimaryDocument,   // a workspace — task-switchable, taskbar, primary identity
  WorkspaceUtility,  // palette/inspector — owned by a workspace, no task-switch
  GlobalUtility,     // the ecology launcher — global control, no task-switch
  TransientOverlay,  // an overlay — topmost, transient, no task-switch
};

struct NativeSurfaceBehavior {
  bool appears_in_alt_tab = true;
  bool appears_in_taskbar = true;
  bool transient = false;                  // dies with its context (overlay)
  bool follows_workspace_activation = false;  // raised with its workspace cluster
  bool owned_by_parent_hwnd = false;       // Win32 owner chain to a parent HWND
  bool topmost = false;                    // overlays sit above siblings
  SurfaceShellBehavior shell_behavior = SurfaceShellBehavior::PrimaryDocument;
};

inline const char* ToString(SurfaceShellBehavior b) {
  switch (b) {
    case SurfaceShellBehavior::PrimaryDocument:  return "primary_document";
    case SurfaceShellBehavior::WorkspaceUtility: return "workspace_utility";
    case SurfaceShellBehavior::GlobalUtility:    return "global_utility";
    case SurfaceShellBehavior::TransientOverlay: return "transient_overlay";
  }
  return "?";
}

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_NATIVE_SURFACE_BEHAVIOR_H_
