#include "win32_surface_realizer.h"
#include <windows.h>

namespace morphic {

Win32SurfaceRealizer::Win32SurfaceRealizer(SurfaceRegistry& registry, ActivationManager& actMgr)
    : registry_(registry), actMgr_(actMgr) {}

void Win32SurfaceRealizer::realizeTopology(NodeId surfaceId, const Transform& desiredGeom, bool desiredVisible, SurfaceRole role) {
    auto* host = registry_.getHost(surfaceId);
    if (!host || !host->isAlive()) return;

    HWND hwnd = host->hwnd();
    
    // Apply topology styling if changed
    if (host->currentRole() != role) {
        topoMgr_.applyTopology(hwnd, role, registry_.ownerWindow());
    }

    // Apply geometry if changed (checks against current physical metrics to avoid redundant calls)
    RECT wr;
    GetWindowRect(hwnd, &wr);
    Transform physicalGeom = Transform::fromRECT(wr);
    if (physicalGeom != desiredGeom) {
        host->updatePosition(desiredGeom);
    }

    // Apply visibility if changed
    bool isPhysicallyVisible = (IsWindowVisible(hwnd) != FALSE);
    if (isPhysicallyVisible != desiredVisible) {
        if (desiredVisible) {
            host->show();
        } else {
            host->hide();
        }
    }
}

void Win32SurfaceRealizer::realizeElevation(NodeId surfaceId, ElevationLayer layer, int sublevel) {
    auto* host = registry_.getHost(surfaceId);
    if (!host || !host->isAlive()) return;
    
    auto* surface = host->surface();
    if (surface) {
        surface->setElevation(layer, sublevel);
    }
}

void Win32SurfaceRealizer::realizeActivation(NodeId surfaceId, bool desiredActive) {
    if (desiredActive) {
        actMgr_.requestActivation(surfaceId);
    }
}

void Win32SurfaceRealizer::notifyRendererResize(NodeId surfaceId, int width, int height) {
    auto* host = registry_.getHost(surfaceId);
    if (host && host->isAlive() && host->renderer() && host->renderer()->isCreated()) {
        // Enforce the Renderer Notification Barrier:
        // Resize child renderer window safely outside synchronous layout handlers.
        host->renderer()->resize(width, height);
    }
}

} // namespace morphic
