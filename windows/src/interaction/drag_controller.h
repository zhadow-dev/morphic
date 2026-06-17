#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include <windows.h>

namespace morphic {

// Screen-space group drag synchronization.
// Uses GetCursorPos exclusively — never Flutter deltas.
class DragController {
public:
    bool isDragging() const { return dragging_; }
    NodeId dragSurfaceId() const { return dragSurfaceId_; }
    NodeId dragGroupId() const { return dragGroupId_; }

    void beginDrag(SceneGraph& graph, NodeId surfaceId, POINT screenPos) {
        dragSurfaceId_ = surfaceId;
        lastPos_ = screenPos;
        dragging_ = true;

        // Determine if this surface belongs to a group
        dragGroupId_ = graph.getGroupForSurface(surfaceId);

        // Snapshot initial positions of all surfaces being dragged
        dragOffsets_.clear();
        if (dragGroupId_ != kInvalidNodeId) {
            auto members = graph.getGroupMembers(dragGroupId_);
            for (auto mid : members) {
                auto* s = graph.getSurface(mid);
                if (s) {
                    dragOffsets_[mid] = { s->localTransform().x, s->localTransform().y };
                }
            }
        } else {
            auto* s = graph.getSurface(surfaceId);
            if (s) {
                dragOffsets_[surfaceId] = { s->localTransform().x, s->localTransform().y };
            }
        }

        dragStartPos_ = screenPos;
    }

    // Returns the set of surface IDs and their new positions
    struct DragUpdate {
        NodeId surfaceId;
        int newX, newY;
    };

    std::vector<DragUpdate> updateDrag(POINT screenPos) {
        if (!dragging_) return {};

        int dx = screenPos.x - dragStartPos_.x;
        int dy = screenPos.y - dragStartPos_.y;
        lastPos_ = screenPos;

        std::vector<DragUpdate> updates;
        for (auto& [sid, origin] : dragOffsets_) {
            updates.push_back({ sid, origin.x + dx, origin.y + dy });
        }
        return updates;
    }

    void endDrag() {
        dragging_ = false;
        dragSurfaceId_ = kInvalidNodeId;
        dragGroupId_ = kInvalidNodeId;
        dragOffsets_.clear();
    }

private:
    bool dragging_ = false;
    NodeId dragSurfaceId_ = kInvalidNodeId;
    NodeId dragGroupId_ = kInvalidNodeId;
    POINT lastPos_ = {};
    POINT dragStartPos_ = {};

    struct Offset { int x, y; };
    std::unordered_map<NodeId, Offset> dragOffsets_;
};

}  // namespace morphic
