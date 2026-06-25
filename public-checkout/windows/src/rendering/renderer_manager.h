#pragma once

#include "renderer_surface.h"
#include "activity_state.h"
#include "visibility_state.h"
#include "../core/types.h"
#include "../core/thread_affinity.h"
#include <windows.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>

namespace morphic {

// Phase 2A — Central renderer registry.
//
// RendererManager owns ALL renderer instances across the process.
// WindowHost borrows a raw pointer to the active renderer.
//
// This is necessary because:
//   renderer lifetime > surface lifetime
//
// Flutter engines that are "detached" or "hidden" remain alive in memory
// because destroying a secondary engine crashes the shared Dart VM.
// These zombie engines must be tracked explicitly.
//
// Responsibilities:
//   1. Owns renderer instances (unique_ptr ownership)
//   2. Tracks lifecycle + attachment state per renderer
//   3. Provides aggregate metrics (active count, zombie count, memory estimates)
//   4. Coordinates shutdown ordering (process exit)
//   5. Future: VM coordination, shared-engine pivot, renderer pooling
//
// IMPORTANT: destroyAll() at process exit assumes process termination
// performs OS-level cleanup. We do NOT assume Flutter engine destruction
// is safe during shutdown sequencing. The OS reclaims resources.
class RendererManager {
public:
    // Per-engine record — tracks everything about a renderer instance.
    struct EngineRecord {
        RenderId id = kInvalidRenderId;
        NodeId surfaceId = kInvalidNodeId;  // Surface it was last bound to
        RendererSurface::Type type = RendererSurface::Type::Null;
        RendererLifecycle lifecycle = RendererLifecycle::Uninitialized;
        RendererAttachment attachment = RendererAttachment::Unattached;

        // Phase 2B: Orthogonal activity state (observable behavior).
        // Independent of lifecycle and attachment.
        ActivityState activity = ActivityState::Active;
        ActivityClassification classification = ActivityClassification::Healthy;
        VisibilityState visibility = VisibilityState::Unknown;

        // Timing
        std::chrono::high_resolution_clock::time_point createdAt;
        std::chrono::high_resolution_clock::time_point zombifiedAt;
        std::chrono::high_resolution_clock::time_point lastActivityTransition;
        double startupMs = 0.0;

        // Frame metrics (snapshot from renderer)
        int64_t totalFramesRendered = 0;
        uint64_t lastProducedFrame = 0;
        uint64_t lastCompositionCommitFrame = 0;

        // Convenience
        bool isAlive() const {
            return lifecycle == RendererLifecycle::Running ||
                   lifecycle == RendererLifecycle::Suspended;
        }
        bool isZombie() const { return lifecycle == RendererLifecycle::Zombie; }
        bool isBound() const { return attachment == RendererAttachment::Attached; }

        // Phase 3E.3: Surface role for governance-aware decisions
        SurfaceRole surfaceRole = SurfaceRole::Workspace;
    };

    // Aggregate metrics for debug overlay and getMetrics.
    struct ManagerMetrics {
        size_t totalCreated = 0;     // Total renderers ever created
        size_t activeCount = 0;      // Currently Running + Attached
        size_t zombieCount = 0;      // Zombie state
        size_t hiddenCount = 0;      // Hidden but alive
        size_t failedCount = 0;      // Failed to create
        size_t flutterEngines = 0;   // Total Flutter-type engines (active + zombie)
    };

    RendererManager() = default;
    ~RendererManager() = default;

    // --- Registration ---

    // Register a new renderer instance. Takes ownership.
    // Returns the assigned RenderId.
    // Called by Compositor::attachRenderer before WindowHost::attachRenderer.
    RenderId registerRenderer(std::unique_ptr<RendererSurface> renderer,
                              NodeId surfaceId) {
        MORPHIC_ASSERT_UI_THREAD();
        RenderId id = nextId_++;

        renderer->setRendererId(id);
        renderer->setLifecycle(RendererLifecycle::Uninitialized);
        renderer->setAttachment(RendererAttachment::Unattached);

        EngineRecord record;
        record.id = id;
        record.surfaceId = surfaceId;
        record.type = renderer->type();
        record.lifecycle = RendererLifecycle::Uninitialized;
        record.attachment = RendererAttachment::Unattached;
        record.createdAt = std::chrono::high_resolution_clock::now();

        records_[id] = record;
        renderers_[id] = std::move(renderer);

        OutputDebugStringA(("RENDERER_MGR: Registered renderer #" +
            std::to_string(id) + " type=" +
            renderers_[id]->typeName() + " for surface #" +
            std::to_string(surfaceId) + "\n").c_str());

        return id;
    }

    // --- State transitions ---

    // Transition to Running + Attached after successful create().
    void markRunning(RenderId id) {
        MORPHIC_ASSERT_UI_THREAD();
        transitionLifecycle(id, RendererLifecycle::Running);
        transitionAttachment(id, RendererAttachment::Attached);
    }

    // Transition to Failed after create() failure.
    void markFailed(RenderId id) {
        MORPHIC_ASSERT_UI_THREAD();
        transitionLifecycle(id, RendererLifecycle::Failed);
        transitionAttachment(id, RendererAttachment::Unattached);
    }

    // Transition to Hidden (hideWithoutDestroy path).
    // Engine stays alive in memory but is invisible.
    void markHidden(RenderId id) {
        MORPHIC_ASSERT_UI_THREAD();
        transitionAttachment(id, RendererAttachment::Hidden);
        syncMetrics(id);
    }

    // Transition to Zombie (can't be destroyed due to VM safety).
    void markZombie(RenderId id) {
        MORPHIC_ASSERT_UI_THREAD();
        transitionLifecycle(id, RendererLifecycle::Zombie);
        transitionAttachment(id, RendererAttachment::Detached);

        if (auto it = records_.find(id); it != records_.end()) {
            it->second.zombifiedAt = std::chrono::high_resolution_clock::now();
        }

        OutputDebugStringA(("RENDERER_MGR: Renderer #" +
            std::to_string(id) + " -> ZOMBIE\n").c_str());
    }

    // Transition to Detached (unparented but alive — future reattach).
    void markDetached(RenderId id) {
        MORPHIC_ASSERT_UI_THREAD();
        transitionAttachment(id, RendererAttachment::Detached);
    }

    // Phase X: Hard removal — completely erase renderer from all tracking.
    // Called AFTER renderer has been destroyed and all references cleared.
    // Returns the unique_ptr so caller can ensure destruction ordering.
    std::unique_ptr<RendererSurface> removeRenderer(RenderId id) {
        MORPHIC_ASSERT_UI_THREAD();
        std::unique_ptr<RendererSurface> removed;
        auto rendIt = renderers_.find(id);
        if (rendIt != renderers_.end()) {
            removed = std::move(rendIt->second);
            renderers_.erase(rendIt);
        }
        records_.erase(id);
        OutputDebugStringA(("RENDERER_MGR: REMOVED renderer #" +
            std::to_string(id) + " (hard destroy)\n").c_str());
        return removed;
    }

    // --- Phase 2B: Activity state transitions ---
    // Activity state is independent of lifecycle/attachment.
    // Managed by WorkloadController, not by lifecycle events.

    void transitionActivity(RenderId id, ActivityState newState) {
        MORPHIC_ASSERT_UI_THREAD();
        auto it = records_.find(id);
        if (it == records_.end()) return;
        auto& rec = it->second;
        if (rec.activity == newState) return;

        OutputDebugStringA(("RENDERER_MGR: #" + std::to_string(id) +
            " activity " + toString(rec.activity) +
            " -> " + toString(newState) + "\n").c_str());

        rec.activity = newState;
        rec.lastActivityTransition = std::chrono::high_resolution_clock::now();
    }

    void setClassification(RenderId id, ActivityClassification c) {
        auto it = records_.find(id);
        if (it != records_.end()) it->second.classification = c;
    }

    void setVisibility(RenderId id, VisibilityState v) {
        auto it = records_.find(id);
        if (it != records_.end()) it->second.visibility = v;
    }

    ActivityState activityOf(RenderId id) const {
        auto it = records_.find(id);
        return (it != records_.end()) ? it->second.activity : ActivityState::Active;
    }

    // --- Queries ---

    // Get the raw pointer for WindowHost to borrow.
    // Returns nullptr if not found or not alive.
    RendererSurface* getRenderer(RenderId id) {
        auto it = renderers_.find(id);
        return (it != renderers_.end()) ? it->second.get() : nullptr;
    }

    const EngineRecord* getRecord(RenderId id) const {
        auto it = records_.find(id);
        return (it != records_.end()) ? &it->second : nullptr;
    }

    // Phase 3E.3: Mutable record access for role updates.
    EngineRecord* getMutableRecord(RenderId id) {
        auto it = records_.find(id);
        return (it != records_.end()) ? &it->second : nullptr;
    }

    // Phase 2B.1: Sync metrics for ALL renderers from live instances.
    // Must be called before snapshotting to get current frame counts.
    void syncAllMetrics() {
        for (auto& [id, rec] : records_) {
            syncMetrics(id);
        }
    }

    // Phase 2B.1: Get total frames across all renderers.
    int64_t totalFramesRendered() const {
        int64_t total = 0;
        for (const auto& [id, rec] : records_) {
            total += rec.totalFramesRendered;
        }
        return total;
    }

    // Compute aggregate metrics.
    ManagerMetrics computeMetrics() const {
        ManagerMetrics m;
        m.totalCreated = records_.size();

        for (const auto& [id, rec] : records_) {
            if (rec.isAlive() && rec.isBound()) m.activeCount++;
            if (rec.isZombie()) m.zombieCount++;
            if (rec.attachment == RendererAttachment::Hidden) m.hiddenCount++;
            if (rec.lifecycle == RendererLifecycle::Failed) m.failedCount++;
            if (rec.type == RendererSurface::Type::Flutter) m.flutterEngines++;
        }

        return m;
    }

    size_t totalCount() const { return records_.size(); }

    size_t activeCount() const {
        size_t n = 0;
        for (const auto& [id, rec] : records_) {
            if (rec.isAlive() && rec.isBound()) n++;
        }
        return n;
    }

    size_t zombieCount() const {
        size_t n = 0;
        for (const auto& [id, rec] : records_) {
            if (rec.isZombie()) n++;
        }
        return n;
    }

    // Get all records for metrics/overlay.
    const std::unordered_map<RenderId, EngineRecord>& records() const {
        return records_;
    }

    // --- Shutdown ---

    // Called at process exit.
    // WARNING: This does NOT safely destroy Flutter engines.
    // Process termination handles OS resource cleanup.
    // We only destroy non-Flutter renderers explicitly.
    void destroyAll() {
        MORPHIC_ASSERT_UI_THREAD();
        OutputDebugStringA(("RENDERER_MGR: Destroying all — " +
            std::to_string(renderers_.size()) + " renderers\n").c_str());

        for (auto& [id, renderer] : renderers_) {
            if (renderer && renderer->type() != RendererSurface::Type::Flutter) {
                if (renderer->isCreated()) {
                    renderer->destroy();
                }
            }
            // Flutter renderers: let process exit handle cleanup.
            // Explicitly destroying them crashes the shared Dart VM.
        }

        renderers_.clear();
        records_.clear();
    }

private:
    void transitionLifecycle(RenderId id, RendererLifecycle newState) {
        auto recIt = records_.find(id);
        auto rendIt = renderers_.find(id);
        if (recIt != records_.end()) {
            recIt->second.lifecycle = newState;
        }
        if (rendIt != renderers_.end() && rendIt->second) {
            rendIt->second->setLifecycle(newState);
        }
    }

    void transitionAttachment(RenderId id, RendererAttachment newState) {
        auto recIt = records_.find(id);
        auto rendIt = renderers_.find(id);
        if (recIt != records_.end()) {
            recIt->second.attachment = newState;
        }
        if (rendIt != renderers_.end() && rendIt->second) {
            rendIt->second->setAttachment(newState);
        }
    }

    // Sync metrics from the live renderer into the record.
    void syncMetrics(RenderId id) {
        auto recIt = records_.find(id);
        auto rendIt = renderers_.find(id);
        if (recIt != records_.end() && rendIt != renderers_.end() && rendIt->second) {
            const auto& m = rendIt->second->metrics();
            recIt->second.totalFramesRendered = m.totalFramesRendered;
            recIt->second.lastProducedFrame = m.lastProducedFrame;
            recIt->second.lastCompositionCommitFrame = m.lastCompositionCommitFrame;
            recIt->second.startupMs = m.startupMs;
        }
    }

    RenderId nextId_ = 1;  // 0 is kInvalidRenderId
    std::unordered_map<RenderId, std::unique_ptr<RendererSurface>> renderers_;
    std::unordered_map<RenderId, EngineRecord> records_;
};

}  // namespace morphic
