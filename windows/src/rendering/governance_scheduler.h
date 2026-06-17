#pragma once

#include "../core/types.h"
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <string>
#include <windows.h>

namespace morphic {

// Phase 3B.2 — Governance Dirty Reasons
//
// Why governance was invalidated. Domain-grouped to prevent
// infinite enum growth. Each domain covers a category of
// state changes, not individual events.
//
// Design rule: if you need a new reason, it should fit into
// an existing domain. If it doesn't, that's an architecture
// conversation, not a code change.
enum class GovernanceDirtyReason {
    // Visibility domain — occlusion, minimize, show, background
    VisibilityChanged,

    // Workload domain — trait/profile declarations
    WorkloadChanged,

    // Pressure domain — memory, GPU, thermal, battery
    PressureChanged,

    // Interaction domain — user input, focus
    InteractionChanged,

    // Lifecycle domain — recovery completed, churn threshold
    RecoveryCompleted,

    // External domain — test commands, debug overrides
    ExternalCommand,

    // Role domain — surface role changed (Phase 3E.3)
    RoleChanged,
};

inline const char* toString(GovernanceDirtyReason reason) {
    switch (reason) {
        case GovernanceDirtyReason::VisibilityChanged: return "VisibilityChanged";
        case GovernanceDirtyReason::WorkloadChanged:   return "WorkloadChanged";
        case GovernanceDirtyReason::PressureChanged:   return "PressureChanged";
        case GovernanceDirtyReason::InteractionChanged: return "InteractionChanged";
        case GovernanceDirtyReason::RecoveryCompleted: return "RecoveryCompleted";
        case GovernanceDirtyReason::ExternalCommand:   return "ExternalCommand";
        case GovernanceDirtyReason::RoleChanged:        return "RoleChanged";
    }
    return "Unknown";
}

// Phase 3B.2 — Governance Scheduler
//
// Coalesced, debounced governance evaluation.
// Collects dirty flags per-renderer, then evaluates all dirty
// renderers on next drain(). NOT frame-coupled.
//
// Key properties:
//   - Coalescing: multiple dirtys for same renderer = single evaluation
//   - Debounce: won't drain more than once per minDrainIntervalMs_
//   - Domain tracking: records WHY governance was invalidated
//   - Observable: exposes dirty count and last drain time for telemetry
//
// THREAD: UI thread only.
class GovernanceScheduler {
public:
    // Mark a renderer as needing governance reevaluation.
    // Does NOT evaluate immediately — just sets the flag.
    // Multiple calls for the same renderer coalesce into one evaluation.
    void markDirty(RenderId id, GovernanceDirtyReason reason) {
        dirtyRenderers_.insert(id);
        lastDirtyReason_[id] = reason;

        OutputDebugStringA(("GOV_SCHED: dirty #" + std::to_string(id) +
            " reason=" + toString(reason) + "\n").c_str());
    }

    // Mark ALL tracked renderers as dirty (e.g. pressure change affects everyone).
    void markAllDirty(GovernanceDirtyReason reason) {
        for (auto& [id, _] : lastDirtyReason_) {
            dirtyRenderers_.insert(id);
            lastDirtyReason_[id] = reason;
        }

        OutputDebugStringA(("GOV_SCHED: ALL dirty reason=" +
            std::string(toString(reason)) + "\n").c_str());
    }

    // Register a renderer for tracking (call on renderer creation).
    void trackRenderer(RenderId id) {
        lastDirtyReason_[id] = GovernanceDirtyReason::VisibilityChanged;
        // Don't auto-dirty on creation — let initial state settle
    }

    // Unregister a renderer (call on renderer destruction).
    void untrackRenderer(RenderId id) {
        dirtyRenderers_.erase(id);
        lastDirtyReason_.erase(id);
    }

    // Check if any renderers need evaluation.
    bool hasDirty() const { return !dirtyRenderers_.empty(); }

    // Number of dirty renderers.
    int dirtyCount() const { return static_cast<int>(dirtyRenderers_.size()); }

    // Check if enough time has passed since last drain (debounce).
    bool canDrain() const {
        if (dirtyRenderers_.empty()) return false;
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastDrainTime_).count();
        return elapsed >= minDrainIntervalMs_;
    }

    // Get the set of dirty renderer IDs and their reasons.
    // After calling drain(), call clearDirty() to reset.
    const std::unordered_set<RenderId>& dirtyRenderers() const {
        return dirtyRenderers_;
    }

    // Get the dirty reason for a specific renderer.
    GovernanceDirtyReason dirtyReason(RenderId id) const {
        auto it = lastDirtyReason_.find(id);
        return (it != lastDirtyReason_.end()) ?
            it->second : GovernanceDirtyReason::VisibilityChanged;
    }

    // Clear all dirty flags (call after drain completes).
    void clearDirty() {
        auto now = std::chrono::high_resolution_clock::now();
        lastDrainTime_ = now;
        totalDrains_++;

        int count = static_cast<int>(dirtyRenderers_.size());
        dirtyRenderers_.clear();

        OutputDebugStringA(("GOV_SCHED: drained " + std::to_string(count) +
            " renderers (drain #" + std::to_string(totalDrains_) + ")\n").c_str());
    }

    // ---- Telemetry ----
    int totalDrains() const { return totalDrains_; }

private:
    std::unordered_set<RenderId> dirtyRenderers_;
    std::unordered_map<RenderId, GovernanceDirtyReason> lastDirtyReason_;

    std::chrono::high_resolution_clock::time_point lastDrainTime_ =
        std::chrono::high_resolution_clock::now();

    int minDrainIntervalMs_ = 100;  // Don't drain faster than 10Hz
    int totalDrains_ = 0;
};

}  // namespace morphic
