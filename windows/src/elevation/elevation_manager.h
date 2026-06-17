#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include <windows.h>
#include <vector>
#include <algorithm>

namespace morphic {

// Translates semantic elevation (layers + sublevels) to Win32 z-order.
// Uses DeferWindowPos for atomic z-order updates.
class ElevationManager {
public:
    // Resolve all surface z-orders based on elevation.
    // Called when any surface's elevation changes.
    void resolveZOrder(SceneGraph& graph) {
        // Collect all surfaces with hosts
        struct ZEntry {
            int sortKey;    // layer * 1000 + sublevel
            HWND hwnd;
        };

        std::vector<ZEntry> entries;
        graph.forEachSurface([&](CompositionSurface* s) {
            if (!s->hasHost() || !s->host()->hwnd()) return;
            int key = static_cast<int>(s->elevationLayer()) * 1000 + s->elevationSublevel();
            entries.push_back({ key, s->host()->hwnd() });
        });

        if (entries.empty()) return;

        // Sort by elevation: HIGHEST first (they go to the top of z-order)
        std::sort(entries.begin(), entries.end(),
                  [](const ZEntry& a, const ZEntry& b) { return a.sortKey > b.sortKey; });

        // Apply z-order atomically
        HDWP hdwp = BeginDeferWindowPos(static_cast<int>(entries.size()));
        if (!hdwp) return;

        for (size_t i = 0; i < entries.size(); i++) {
            // First (highest) goes to HWND_TOP, rest stack below the previous
            HWND insertAfter = (i == 0) ? HWND_TOP : entries[i - 1].hwnd;
            hdwp = DeferWindowPos(hdwp, entries[i].hwnd, insertAfter,
                                  0, 0, 0, 0,
                                  SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (!hdwp) return;
        }

        EndDeferWindowPos(hdwp);
    }

    // Bring an entire group to front, with a specific surface on top within the group
    void bringGroupToFront(SceneGraph& graph, NodeId groupId, NodeId topSurfaceId = kInvalidNodeId) {
        auto* group = graph.getGroup(groupId);
        if (!group) return;

        // Bump sublevel for all group members EXCEPT the focused one
        for (auto mid : group->memberIds) {
            if (mid == topSurfaceId) continue;  // Skip — will be placed on top
            auto* s = graph.getSurface(mid);
            if (s) {
                s->setElevation(s->elevationLayer(), nextSublevel_++);
            }
        }

        // Focused surface gets the highest sublevel (on top within group)
        if (topSurfaceId != kInvalidNodeId) {
            auto* top = graph.getSurface(topSurfaceId);
            if (top) {
                top->setElevation(top->elevationLayer(), nextSublevel_++);
            }
        }

        resolveZOrder(graph);
    }

    // Bring a single surface to front
    void bringToFront(SceneGraph& graph, NodeId surfaceId) {
        auto* s = graph.getSurface(surfaceId);
        if (!s) return;

        NodeId gid = s->groupId();
        if (gid != kInvalidNodeId) {
            // Bring group to front with THIS surface on top
            bringGroupToFront(graph, gid, surfaceId);
        } else {
            s->setElevation(s->elevationLayer(), nextSublevel_++);
            resolveZOrder(graph);
        }
    }

private:
    int nextSublevel_ = 1;
};

}  // namespace morphic
