#pragma once

#include "types.h"
#include "scene_graph.h"
#include "window_host.h"
#include "surface_role.h"
#include "topology_manager.h"
#include "mutation_authority.h"
#include <memory>
#include <unordered_map>
#include <vector>
#include <functional>

namespace morphic {

class Compositor;

// Phase 4 Step 1A — SurfaceRegistry.
//
// AUTHORITY: Surface lifetime.
// This is the SINGLE authority for creating, destroying, and querying surfaces.
//
// SurfaceRegistry OWNS:
//   - CompositionSurface instances (via SceneGraph)
//   - WindowHost instances
//   - surface → host binding
//   - surface presence tracking
//
// SurfaceRegistry DOES NOT OWN:
//   - topology (TopologyManager does)
//   - z-order / activation (ActivationManager does)
//   - renderer lifecycle (RendererRuntime does, currently proxied via Compositor)
//   - governance (OrchestrationRuntime does)
//   - drag/resize (InteractionRuntime / Compositor does)
//
// SurfaceRegistry NEVER calls:
//   - SetWindowPos, SetParent, BringWindowToTop, SetForegroundWindow
//   - ShowWindow (for visibility toggling — that's Visibility authority)
//
// DEPENDENCY LAYER: Layer 1 (Data)
//   MAY use: types, RuntimePhase
//   MAY NOT: query ActivationManager, TopologyManager, RendererRuntime,
//            OrchestrationRuntime, FocusGraph, or any Layer 2+ subsystem
//
// THREAD: UI thread only (HWND creation via WindowHost).
class SurfaceRegistry {
public:
    SurfaceRegistry() = default;

    // Initialize with owner window (used for Win32 ownership of surface HWNDs).
    void initialize(HWND ownerWindow, Compositor* compositor);

    // --- Surface CRUD (lifetime authority) ---

    // Create surface from config. Returns NodeId or kInvalidNodeId on failure.
    // Creates the logical surface AND the WindowHost (HWND realization).
    NodeId createSurface(const SurfaceConfig& config);

    // Destroy surface. Unbinds host, removes from scene graph.
    // DOES NOT handle renderer cleanup — that must be done by caller
    // (Compositor/RendererRuntime) BEFORE calling this.
    void destroySurface(NodeId id);

    // --- Surface role ---

    void setSurfaceRole(NodeId surfaceId, SurfaceRole role);
    SurfaceRole surfaceRole(NodeId surfaceId) const;

    // --- Group lifecycle ---

    NodeId createGroup(const std::vector<NodeId>& memberIds);
    void destroyGroup(NodeId groupId);

    // --- Queries (read-only) ---

    CompositionSurface* getSurface(NodeId id);
    const CompositionSurface* getSurface(NodeId id) const;
    WindowHost* getHost(NodeId surfaceId);
    const WindowHost* getHost(NodeId surfaceId) const;
    bool hasSurface(NodeId id) const;

    // Iterate all hosts (read-only access for other subsystems).
    const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>& hosts() const {
        return hosts_;
    }

    HWND ownerWindow() const { return ownerWindow_; }

    // --- Scene graph access (needed by Compositor for frame scheduling) ---

    SceneGraph& sceneGraph() { return sceneGraph_; }
    const SceneGraph& sceneGraph() const { return sceneGraph_; }

    // --- Shutdown ---

    void destroyAll();

private:
    WindowHost* createHostForSurface(CompositionSurface* surface);
    void destroyHostForSurface(NodeId surfaceId);

    SceneGraph sceneGraph_;
    TopologyManager topologyManager_;
    std::unordered_map<NodeId, std::unique_ptr<WindowHost>> hosts_;
    HWND ownerWindow_ = nullptr;
    Compositor* compositor_ = nullptr;  // Back-pointer for WindowHost event routing
                                         // (temporary — removed in Step 2 when events
                                         // route through RuntimeEventBus instead)
};

}  // namespace morphic
