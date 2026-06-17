#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include <windows.h>

namespace morphic {

// Throttled resize controller.
// Intercepts WM_SIZE and updates scene graph.
// Phase 1A: simple — just update the resized surface's transform.
// No constraint propagation (deferred to Phase 3).
class ResizeController {
public:
    void onSurfaceResized(SceneGraph& graph, NodeId surfaceId, int newWidth, int newHeight) {
        auto* s = graph.getSurface(surfaceId);
        if (!s) return;

        // Apply constraints
        const auto& c = s->constraints();
        if (c.minWidth > 0) newWidth = (std::max)(newWidth, c.minWidth);
        if (c.minHeight > 0) newHeight = (std::max)(newHeight, c.minHeight);
        if (c.maxWidth > 0) newWidth = (std::min)(newWidth, c.maxWidth);
        if (c.maxHeight > 0) newHeight = (std::min)(newHeight, c.maxHeight);

        s->setSize(newWidth, newHeight);
    }

    void setThrottleInterval(int ms) { throttleMs_ = ms; }
    int throttleInterval() const { return throttleMs_; }

private:
    int throttleMs_ = 16;  // ~60fps default
};

}  // namespace morphic
