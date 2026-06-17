#pragma once

#include "../core/types.h"
#include <unordered_map>
#include <vector>
#include <algorithm>

namespace morphic {

// Phase 7B: Attention Economics.
//
// ARCHITECTURAL INVARIANT:
// Attention economics is an ADVISORY layer.
// It prioritizes orchestration suggestions, influences restoration ordering,
// suggests continuity preference, and ranks workflow preservation.
//
// It MUST NEVER:
//   - evict surfaces
//   - degrade runtime state
//   - suspend renderers
//   - throttle scheduler
//   - mutate topology
//
// Those remain runtime governance authority (Phase 6 pressure/degradation).
// This layer provides INTERPRETIVE context consumed by AdaptiveOrchestrator.

enum class AttentionLevel {
    Active,             // User is directly interacting
    PassiveMonitoring,  // Visible but not focused (e.g., log tail)
    LatentContinuity,   // Hidden but continuity must be preserved
    Interruptible,      // Can be suspended without data loss
    Urgent,             // Not focused but semantically high priority (e.g., alert)
    Background          // Lowest priority
};

inline const char* toString(AttentionLevel level) {
    switch (level) {
        case AttentionLevel::Active:            return "Active";
        case AttentionLevel::PassiveMonitoring: return "PassiveMonitoring";
        case AttentionLevel::LatentContinuity:  return "LatentContinuity";
        case AttentionLevel::Interruptible:     return "Interruptible";
        case AttentionLevel::Urgent:            return "Urgent";
        case AttentionLevel::Background:        return "Background";
    }
    return "Unknown";
}

// Advisory cost metric. NOT consumed by runtime for operational decisions.
// Consumed only by AdaptiveOrchestrator for suggestion ranking.
// Let's compress the naming if needed or keep it as it's an experimental advisory.
struct AttentionCost {
    double renderCost = 1.0;       // Estimated renderer resource consumption
    double memoryCost = 1.0;       // Estimated memory footprint
    double continuityWeight = 1.0; // How critical is continuity preservation?

    double totalCost() const {
        return renderCost + memoryCost + continuityWeight;
    }
};

// Per-surface attention state (advisory).
struct SurfaceAttentionState {
    NodeId surfaceId = kInvalidNodeId;
    AttentionLevel level = AttentionLevel::Background;
    AttentionCost cost;

    // Advisory priority score for suggestion ranking.
    // Higher = more important to preserve. NOT an eviction score.
    int preservationPriority() const {
        switch (level) {
            case AttentionLevel::Active:            return 100;
            case AttentionLevel::Urgent:             return 90;
            case AttentionLevel::PassiveMonitoring:  return 60;
            case AttentionLevel::LatentContinuity:   return 40;
            case AttentionLevel::Interruptible:      return 20;
            case AttentionLevel::Background:         return 10;
        }
        return 0;
    }
};

// Phase 7B: Attention Budget.
//
// ADVISORY ONLY. Tracks attention across surfaces.
// Provides: restoration ordering suggestions, preservation priority rankings,
// and over-budget advisory signals.
//
// Does NOT evict, degrade, suspend, or throttle.
// Runtime governance (Phase 6) makes those decisions independently.
class AttentionBudget {
public:
    AttentionBudget() = default;

    void setCapacity(double capacity) { capacity_ = capacity; }
    double capacity() const { return capacity_; }

    void setSurfaceAttention(NodeId surfaceId, AttentionLevel level,
                             AttentionCost cost = {}) {
        SurfaceAttentionState state;
        state.surfaceId = surfaceId;
        state.level = level;
        state.cost = cost;
        surfaces_[surfaceId] = state;
    }

    void removeSurface(NodeId surfaceId) {
        surfaces_.erase(surfaceId);
    }

    AttentionLevel attentionLevel(NodeId surfaceId) const {
        auto it = surfaces_.find(surfaceId);
        if (it != surfaces_.end()) return it->second.level;
        return AttentionLevel::Background;
    }

    double totalCost() const {
        double total = 0.0;
        for (const auto& [id, state] : surfaces_) {
            total += state.cost.totalCost();
        }
        return total;
    }

    // Advisory signal: attention cost exceeds capacity.
    // This is information for the AdaptiveOrchestrator, NOT a trigger
    // for runtime degradation.
    bool isOverBudget() const {
        return totalCost() > capacity_;
    }

    // Advisory: surfaces ordered by preservation priority (highest first).
    // Used by AdaptiveOrchestrator for restoration ordering suggestions.
    std::vector<NodeId> restorationPriorityOrder() const {
        std::vector<std::pair<int, NodeId>> scored;
        for (const auto& [id, state] : surfaces_) {
            scored.push_back({state.preservationPriority(), id});
        }
        // Sort descending: highest priority first
        std::sort(scored.begin(), scored.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        std::vector<NodeId> result;
        for (const auto& [score, id] : scored) {
            result.push_back(id);
        }
        return result;
    }

    size_t surfaceCount() const { return surfaces_.size(); }

private:
    double capacity_ = 100.0;
    std::unordered_map<NodeId, SurfaceAttentionState> surfaces_;
};

} // namespace morphic
