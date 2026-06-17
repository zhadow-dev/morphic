#pragma once

#include "types.h"

namespace morphic {

class RuntimeSurfaceRealizer {
public:
    virtual ~RuntimeSurfaceRealizer() = default;

    // Realizes physical boundary adjustments (Stage 5)
    virtual void realizeTopology(NodeId surfaceId, const Transform& desiredGeom, bool desiredVisible, SurfaceRole role) = 0;
    
    // Realizes elevation transitions (Stage 5)
    virtual void realizeElevation(NodeId surfaceId, ElevationLayer layer, int sublevel) = 0;

    // Realizes z-order and focus activation (Stage 5)
    virtual void realizeActivation(NodeId surfaceId, bool desiredActive) = 0;

    // Post-commit notify renderer resize (Stage 7)
    virtual void notifyRendererResize(NodeId surfaceId, int width, int height) = 0;
};

} // namespace morphic
