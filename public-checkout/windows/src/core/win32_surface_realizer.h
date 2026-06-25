#pragma once

#include "runtime_surface_realizer.h"
#include "surface_registry.h"
#include "topology_manager.h"
#include "../interaction/activation_manager.h"

namespace morphic {

class Win32SurfaceRealizer : public RuntimeSurfaceRealizer {
public:
    Win32SurfaceRealizer(SurfaceRegistry& registry, ActivationManager& actMgr);
    ~Win32SurfaceRealizer() override = default;

    void realizeTopology(NodeId surfaceId, const Transform& desiredGeom, bool desiredVisible, SurfaceRole role) override;
    void realizeElevation(NodeId surfaceId, ElevationLayer layer, int sublevel) override;
    void realizeActivation(NodeId surfaceId, bool desiredActive) override;
    void notifyRendererResize(NodeId surfaceId, int width, int height) override;

private:
    SurfaceRegistry& registry_;
    TopologyManager topoMgr_;
    ActivationManager& actMgr_;
};

} // namespace morphic
