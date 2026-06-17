#include "surface_policy/native_surface_policy.h"

namespace morphic::policy {

NativeSurfaceBehavior ResolveNativeBehavior(const SurfaceDescriptor& d) {
  NativeSurfaceBehavior b;
  switch (d.kind) {
    case SurfaceKind::Workspace:
    case SurfaceKind::DetachedWorkspace:
      // Primary documents: first-class app citizens.
      b.appears_in_alt_tab = true;
      b.appears_in_taskbar = true;
      b.transient = false;
      b.follows_workspace_activation = false;  // they ARE the root, not a follower
      b.owned_by_parent_hwnd = false;
      b.topmost = false;
      b.shell_behavior = SurfaceShellBehavior::PrimaryDocument;
      break;

    case SurfaceKind::ToolPalette:
    case SurfaceKind::Inspector:
    case SurfaceKind::Utility:
      // Workspace utilities: owned, no task-switch presence, follow their workspace.
      b.appears_in_alt_tab = false;
      b.appears_in_taskbar = false;
      b.transient = false;
      b.follows_workspace_activation = true;
      b.owned_by_parent_hwnd = true;   // Win32 owner = the workspace/parent HWND
      b.topmost = false;
      b.shell_behavior = SurfaceShellBehavior::WorkspaceUtility;
      break;

    case SurfaceKind::Overlay:
    case SurfaceKind::Command:
      // Transient overlays: topmost, owned by their spawner, never task-switchable.
      b.appears_in_alt_tab = false;
      b.appears_in_taskbar = false;
      b.transient = true;
      b.follows_workspace_activation = false;
      b.owned_by_parent_hwnd = true;
      b.topmost = true;
      b.shell_behavior = SurfaceShellBehavior::TransientOverlay;
      break;

    case SurfaceKind::EcologyLauncher:
      // Global meta-control: no task-switch presence, no owner (global), not
      // transient, not a workspace follower. Independent global utility.
      b.appears_in_alt_tab = false;
      b.appears_in_taskbar = false;
      b.transient = false;
      b.follows_workspace_activation = false;
      b.owned_by_parent_hwnd = false;
      b.topmost = false;
      b.shell_behavior = SurfaceShellBehavior::GlobalUtility;
      break;
  }
  return b;
}

DWORD ToExStyle(const NativeSurfaceBehavior& b) {
  DWORD ex = 0;
  // WS_EX_TOOLWINDOW removes a window from BOTH Alt+Tab and the taskbar. A window
  // is a task-switch citizen only when it's APPWINDOW (and not TOOLWINDOW).
  if (b.appears_in_alt_tab || b.appears_in_taskbar) {
    ex |= WS_EX_APPWINDOW;
  } else {
    ex |= WS_EX_TOOLWINDOW;
  }
  if (b.topmost) ex |= WS_EX_TOPMOST;
  return ex;
}

}  // namespace morphic::policy
