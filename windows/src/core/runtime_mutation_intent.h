#pragma once

#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include <chrono>
#include "types.h"
#include "../composition/workspace_controller.h"
#include "../experimental/attention_economics.h"

namespace morphic {

enum class MutationPriority {
    Critical = 0,      // Destructions, immediate focus shifts, terminations
    Interactive = 1,   // Direct drag, window resizes, manual mouse actions
    Deferred = 2,      // Smooth animations, transition steps, workspace changes
    Background = 3     // Non-visible garbage collection, telemetry
};

struct StableSortKey {
    MutationPriority priority;
    WorkspaceId workspaceId;
    NodeId surfaceId;
    uint64_t enqueueEpoch;

    bool operator<(const StableSortKey& o) const {
        if (priority != o.priority) {
            return priority < o.priority; // Lower value = higher priority
        }
        if (workspaceId.value != o.workspaceId.value) {
            return workspaceId.value < o.workspaceId.value;
        }
        if (surfaceId != o.surfaceId) {
            return surfaceId < o.surfaceId;
        }
        return enqueueEpoch < o.enqueueEpoch;
    }
};

struct RuntimeMutationIntent {
    enum class Type {
        Destroy,
        GeometryChange,
        VisibilityChange,
        RoleChange,
        ElevationChange,
        ActivationChange,
        OrchChange
    };

    Type type;
    NodeId surfaceId = kInvalidNodeId;
    WorkspaceId workspaceId = WorkspaceId::defaultId();
    MutationPriority priority = MutationPriority::Background;
    uint64_t enqueueEpoch = 0;  // Assigned on enqueue
    uint64_t ageTicks = 0;      // Tracks lifetime in ticks
    uint64_t surfaceEpoch = 0;  // Assigned on enqueue
    uint64_t enqueueTimeMs = 0; // Assigned on enqueue or creation

    // Payload details
    Transform geometry;
    bool visible = false;
    SurfaceRole role = SurfaceRole::Workspace;
    ElevationLayer elevation = ElevationLayer::Base;
    int sublevel = 0;
    bool active = false;

    // Orchestration payload details
    ContinuityState continuity = ContinuityState::Coherent;
    AttentionLevel attention = AttentionLevel::Background;
    SemanticVisibility semanticVisibility = SemanticVisibility::Full;
    RuntimePresence presence = RuntimePresence::ResidencyBudgeted;

    StableSortKey getSortKey() const {
        return { priority, workspaceId, surfaceId, enqueueEpoch };
    }
};

class RuntimeMutationQueue {
public:
    RuntimeMutationQueue() = default;
    ~RuntimeMutationQueue() = default;

    void enqueue(RuntimeMutationIntent intent) {
        intent.enqueueEpoch = currentEpoch_;
        // Invalidation safety: bind to current surface epoch
        intent.surfaceEpoch = surfaceEpochs_[intent.surfaceId];
        if (intent.enqueueTimeMs == 0) {
            auto now = std::chrono::steady_clock::now();
            intent.enqueueTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        }
        queue_.push_back(intent);
    }

    void ageAndEscalateIntents() {
        for (auto& intent : queue_) {
            intent.ageTicks++;
            if (intent.ageTicks >= 5) {
                intent.ageTicks = 0; // Reset age to wait another 5 ticks if needed
                if (intent.priority != MutationPriority::Critical) {
                    intent.priority = static_cast<MutationPriority>(static_cast<int>(intent.priority) - 1);
                }
            }
        }
    }

    void invalidateSurfaceIntents(NodeId surfaceId) {
        // Increment the epoch for this surface so that any future intents matching it must match the new epoch,
        // and instantly purge/erase all existing ones in the queue for this surface!
        surfaceEpochs_[surfaceId]++;
        
        queue_.erase(
            std::remove_if(queue_.begin(), queue_.end(),
                [surfaceId](const RuntimeMutationIntent& intent) {
                    return intent.surfaceId == surfaceId;
                }),
            queue_.end());
    }

    void invalidateWorkspaceIntents(WorkspaceId workspaceId) {
        queue_.erase(
            std::remove_if(queue_.begin(), queue_.end(),
                [workspaceId](const RuntimeMutationIntent& intent) {
                    return intent.workspaceId.value == workspaceId.value;
                }),
            queue_.end());
    }

    void clear() {
        queue_.clear();
    }

    void sort() {
        std::stable_sort(queue_.begin(), queue_.end(),
            [](const RuntimeMutationIntent& a, const RuntimeMutationIntent& b) {
                return a.getSortKey() < b.getSortKey();
            });
    }

    std::vector<RuntimeMutationIntent>& intents() { return queue_; }
    const std::vector<RuntimeMutationIntent>& intents() const { return queue_; }

    void advanceEpoch() {
        currentEpoch_++;
    }

    uint64_t currentEpoch() const { return currentEpoch_; }
    
    uint64_t getSurfaceEpoch(NodeId surfaceId) const {
        auto it = surfaceEpochs_.find(surfaceId);
        return it != surfaceEpochs_.end() ? it->second : 0;
    }

private:
    std::vector<RuntimeMutationIntent> queue_;
    uint64_t currentEpoch_ = 0;
    std::unordered_map<NodeId, uint64_t> surfaceEpochs_;
};

} // namespace morphic
