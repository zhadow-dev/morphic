#pragma once

#include "types.h"

namespace morphic {

// Phase 4 — Surface Presence Model.
//
// A LogicalSurface may exist WITHOUT an HWND.
// This is the critical mental separation:
//
//   LogicalSurface != WindowHost
//
// CompositionSurface is the logical entity.
// WindowPresence is whether it has an HWND on screen.
// RendererPresence is whether it has a bound renderer.
// WorkspacePresence is whether it belongs to a visible workspace.
//
// Surfaces without HWND presence:
//   - virtual surfaces (serialized workspace restore)
//   - overview thumbnails (rendered from cached bitmap)
//   - suspended surfaces (parked engine, HWND destroyed)
//   - offscreen renderers (pre-warming)
//   - detached persistence (closed window, recoverable)
//
// DESIGN RULE:
//   No subsystem may assume surface implies HWND.
//   Always check presence before HWND operations.
//
// THREAD: UI thread only.

enum class WindowPresence {
    Realized,       // Has an active HWND on screen
    Unrealized,     // Logical surface exists, no HWND
    Suspended,      // HWND was destroyed, can be re-realized
};

enum class RendererPresence {
    Bound,          // Renderer actively producing frames
    Unbound,        // No renderer assigned
    Zombie,         // Renderer detached, engine still resident
    Crashed,        // Renderer was bound but failed
};

enum class WorkspacePresence {
    Active,         // Surface is in the currently visible workspace
    Inactive,       // Surface belongs to a non-visible workspace
    Pinned,         // Surface is visible in ALL workspaces
    Orphaned,       // Surface lost its workspace (error state)
};

// Combined presence state for a surface.
// This is the source of truth for "what does this surface have?"
struct SurfacePresence {
    WindowPresence window = WindowPresence::Unrealized;
    RendererPresence renderer = RendererPresence::Unbound;
    WorkspacePresence workspace = WorkspacePresence::Active;

    bool hasWindow() const { return window == WindowPresence::Realized; }
    bool hasRenderer() const { return renderer == RendererPresence::Bound; }
    bool isVisible() const { return hasWindow() && workspace != WorkspacePresence::Inactive; }
    bool canReceiveInput() const { return hasWindow() && workspace == WorkspacePresence::Active; }
};

}  // namespace morphic
