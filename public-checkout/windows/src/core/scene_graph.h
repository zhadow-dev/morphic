#pragma once

#include "types.h"
#include "composition_surface.h"
#include "window_host.h"
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <algorithm>

namespace morphic {

class Compositor;

// Group of surfaces that move/elevate together.
struct SurfaceGroup {
    NodeId id = kInvalidNodeId;
    std::vector<NodeId> memberIds;

    RECT computeBounds(const std::unordered_map<NodeId, std::unique_ptr<CompositionSurface>>& surfaces) const {
        RECT bounds = { LONG_MAX, LONG_MAX, LONG_MIN, LONG_MIN };
        for (auto mid : memberIds) {
            auto it = surfaces.find(mid);
            if (it == surfaces.end()) continue;
            auto& t = it->second->worldTransform();
            bounds.left   = (std::min)(bounds.left,   (LONG)t.x);
            bounds.top    = (std::min)(bounds.top,    (LONG)t.y);
            bounds.right  = (std::max)(bounds.right,  (LONG)(t.x + t.width));
            bounds.bottom = (std::max)(bounds.bottom, (LONG)(t.y + t.height));
        }
        return bounds;
    }
};

// The scene graph — owns all visual nodes and their relationships.
class SceneGraph {
public:
    SceneGraph() = default;

    // --- Surface lifecycle ---
    NodeId createSurface(const SurfaceConfig& config) {
        NodeId id = nextId_++;
        auto surface = std::make_unique<CompositionSurface>(id, config);
        surfaces_[id] = std::move(surface);
        return id;
    }

    void destroySurface(NodeId id) {
        auto it = surfaces_.find(id);
        if (it == surfaces_.end()) return;

        // Remove from any group
        for (auto& [gid, group] : groups_) {
            auto& members = group.memberIds;
            members.erase(std::remove(members.begin(), members.end(), id), members.end());
        }

        // Unbind host
        if (it->second->hasHost()) {
            it->second->unbindFromHost();
        }

        surfaces_.erase(it);
    }

    CompositionSurface* getSurface(NodeId id) const {
        auto it = surfaces_.find(id);
        return it != surfaces_.end() ? it->second.get() : nullptr;
    }

    // --- Group lifecycle ---
    NodeId createGroup(const std::vector<NodeId>& memberIds) {
        NodeId gid = nextId_++;
        SurfaceGroup group;
        group.id = gid;
        group.memberIds = memberIds;
        groups_[gid] = group;

        for (auto mid : memberIds) {
            auto* s = getSurface(mid);
            if (s) {
                s->setGroupId(gid);
                s->setAttachState(AttachState::Attached);
            }
        }
        return gid;
    }

    void destroyGroup(NodeId groupId) {
        auto it = groups_.find(groupId);
        if (it == groups_.end()) return;

        for (auto mid : it->second.memberIds) {
            auto* s = getSurface(mid);
            if (s) {
                s->setGroupId(kInvalidNodeId);
                s->setAttachState(AttachState::Detached);
            }
        }
        groups_.erase(it);
    }

    SurfaceGroup* getGroup(NodeId groupId) {
        auto it = groups_.find(groupId);
        return it != groups_.end() ? &it->second : nullptr;
    }

    NodeId getGroupForSurface(NodeId surfaceId) const {
        auto* s = getSurface(surfaceId);
        return s ? s->groupId() : kInvalidNodeId;
    }

    std::vector<NodeId> getGroupMembers(NodeId groupId) const {
        auto it = groups_.find(groupId);
        if (it == groups_.end()) return {};
        return it->second.memberIds;
    }

    // --- Dirty resolution ---
    void resolveDirty() {
        for (auto& [id, surface] : surfaces_) {
            surface->computeWorldTransform();
        }
    }

    // Collect all surfaces that are dirty
    std::vector<CompositionSurface*> getDirtySurfaces() {
        std::vector<CompositionSurface*> dirty;
        for (auto& [id, surface] : surfaces_) {
            if (surface->isDirty()) {
                dirty.push_back(surface.get());
            }
        }
        return dirty;
    }

    void clearAllDirty() {
        for (auto& [id, surface] : surfaces_) {
            surface->clearDirty();
            surface->clearElevationDirty();
        }
    }

    // --- Iterators ---
    size_t surfaceCount() const { return surfaces_.size(); }
    size_t groupCount() const { return groups_.size(); }

    void forEachSurface(std::function<void(CompositionSurface*)> fn) {
        for (auto& [id, surface] : surfaces_) {
            fn(surface.get());
        }
    }

    void forEachSurface(std::function<void(const CompositionSurface*)> fn) const {
        for (const auto& [id, surface] : surfaces_) {
            fn(surface.get());
        }
    }

    const std::unordered_map<NodeId, std::unique_ptr<CompositionSurface>>& surfaces() const {
        return surfaces_;
    }

    const std::unordered_map<NodeId, SurfaceGroup>& groups() const {
        return groups_;
    }

private:
    std::unordered_map<NodeId, std::unique_ptr<CompositionSurface>> surfaces_;
    std::unordered_map<NodeId, SurfaceGroup> groups_;
    NodeId nextId_ = 1;
};

}  // namespace morphic
