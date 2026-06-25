#include "surface_registry.h"
#include "../composition/compositor.h"
#include "../core/thread_affinity.h"

namespace morphic {

void SurfaceRegistry::initialize(HWND ownerWindow, Compositor* compositor) {
    ownerWindow_ = ownerWindow;
    compositor_ = compositor;
}

// --- Surface CRUD ---

NodeId SurfaceRegistry::createSurface(const SurfaceConfig& config) {
    MORPHIC_ASSERT_UI_THREAD();

    NodeId id = sceneGraph_.createSurface(config);
    auto* surface = sceneGraph_.getSurface(id);
    if (!surface) return kInvalidNodeId;

    // Compute world transform before creating host
    surface->computeWorldTransform();

    // Create WindowHost (HWND realization)
    auto* host = createHostForSurface(surface);
    if (!host) {
        sceneGraph_.destroySurface(id);
        return kInvalidNodeId;
    }

    // Initial visibility — host->show() is a HWND realization concern,
    // not a visibility-authority concern. The host was just created.
    if (config.visible) {
        host->show();
    }

    return id;
}

void SurfaceRegistry::destroySurface(NodeId id) {
    MORPHIC_ASSERT_UI_THREAD();

    // NOTE: Renderer cleanup MUST be done by caller BEFORE this.
    // SurfaceRegistry does NOT own renderer lifecycle.
    destroyHostForSurface(id);
    sceneGraph_.destroySurface(id);
}

// --- Surface role ---

void SurfaceRegistry::setSurfaceRole(NodeId surfaceId, SurfaceRole role) {
    MORPHIC_ASSERT_UI_THREAD();
    auto* surface = sceneGraph_.getSurface(surfaceId);
    if (!surface) return;

    SurfaceRole oldRole = surface->role();
    if (oldRole == role) return;

    // Update the logical surface role
    surface->setRole(role);

    // Reapply HWND topology via policy derivation.
    // This is topology authority — SurfaceRegistry derives and applies policy
    // because WindowTopologyPolicy is role→style derivation (Layer 0),
    // not topology mutation (Layer 2). The actual SetWindowLongPtr etc.
    // is inside WindowHost::applyTopologyPolicy which is the realization layer.
    auto hostIt = hosts_.find(surfaceId);
    if (hostIt != hosts_.end() && hostIt->second) {
        topologyManager_.applyTopology(hostIt->second->hwnd(), role, ownerWindow_);

        OutputDebugStringA(("SURFACE_REGISTRY: Surface #" + std::to_string(surfaceId) +
            " role " + toString(oldRole) + " -> " + toString(role) + "\n").c_str());
    }
}

SurfaceRole SurfaceRegistry::surfaceRole(NodeId surfaceId) const {
    auto* surface = sceneGraph_.getSurface(surfaceId);
    return surface ? surface->role() : SurfaceRole::Workspace;
}

// --- Group lifecycle ---

NodeId SurfaceRegistry::createGroup(const std::vector<NodeId>& memberIds) {
    return sceneGraph_.createGroup(memberIds);
}

void SurfaceRegistry::destroyGroup(NodeId groupId) {
    sceneGraph_.destroyGroup(groupId);
}

// --- Queries ---

CompositionSurface* SurfaceRegistry::getSurface(NodeId id) {
    return sceneGraph_.getSurface(id);
}

const CompositionSurface* SurfaceRegistry::getSurface(NodeId id) const {
    return sceneGraph_.getSurface(id);
}

WindowHost* SurfaceRegistry::getHost(NodeId surfaceId) {
    auto it = hosts_.find(surfaceId);
    return (it != hosts_.end()) ? it->second.get() : nullptr;
}

const WindowHost* SurfaceRegistry::getHost(NodeId surfaceId) const {
    auto it = hosts_.find(surfaceId);
    return (it != hosts_.end()) ? it->second.get() : nullptr;
}

bool SurfaceRegistry::hasSurface(NodeId id) const {
    return sceneGraph_.getSurface(id) != nullptr;
}

// --- Shutdown ---

void SurfaceRegistry::destroyAll() {
    MORPHIC_ASSERT_UI_THREAD();

    // Disarm all HWNDs before destroying to prevent re-entrant WndProc calls.
    for (auto& [id, host] : hosts_) {
        if (host && host->hwnd()) {
            SetWindowLongPtrW(host->hwnd(), GWLP_USERDATA, 0);
        }
    }

    hosts_.clear();
}

// --- Internal host management ---

WindowHost* SurfaceRegistry::createHostForSurface(CompositionSurface* surface) {
    auto host = std::make_unique<WindowHost>(surface, compositor_, ownerWindow_);
    if (!host->create()) return nullptr;

    surface->bindToHost(host.get());
    auto* rawPtr = host.get();
    hosts_[surface->id()] = std::move(host);
    return rawPtr;
}

void SurfaceRegistry::destroyHostForSurface(NodeId surfaceId) {
    auto* surface = sceneGraph_.getSurface(surfaceId);
    if (surface) surface->unbindFromHost();
    hosts_.erase(surfaceId);
}

}  // namespace morphic
