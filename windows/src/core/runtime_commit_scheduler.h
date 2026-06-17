#pragma once

#include <memory>
#include <vector>
#include <chrono>
#include <mutex>
#include <array>
#include <string>
#include <algorithm>
#include <unordered_map>
#include "types.h"
#include "runtime_scene_state.h"
#include "runtime_mutation_intent.h"

namespace morphic {

class RuntimeSurfaceRealizer;
class Compositor;
class KernelTrace;

constexpr uint32_t kKernelVersion = 1;
constexpr uint32_t kTraceSchemaVersion = 1;

enum class KernelConfidence {
    High,
    Degraded,
    Uncertain
};

inline const char* toString(KernelConfidence conf) {
    switch (conf) {
        case KernelConfidence::High:      return "High";
        case KernelConfidence::Degraded:  return "Degraded";
        case KernelConfidence::Uncertain: return "Uncertain";
    }
    return "Unknown";
}

struct KernelHealth {
    double temporal = 1.0;
    double semantic = 1.0;
    double operational = 1.0;
    double realization = 1.0;
    KernelConfidence confidence = KernelConfidence::High;

    // Recovery Velocity
    double meanDivergenceRepairTicks = 0.0;
    double stabilizationHalfLife = 0.0;
    double meanQuarantineRecoveryTime = 0.0;
    double convergenceVelocity = 0.0;

    // Long-horizon decay
    double repairFrequencyTrend = 0.0;
    double backlogTrendSlope = 0.0;
    size_t quarantineRecurrence = 0;
    double budgetPressure = 0.0;
};

class KernelPanicCoordinator {
public:
    static void registerScheduler(void* scheduler);
    static void panic(const std::string& reason, const std::string& file, int line);
    static void reset();
private:
    static std::mutex mutex_;
    static void* scheduler_;
};

#define MORPHIC_FATAL_KERNEL_VIOLATION(reason) \
    ::morphic::KernelPanicCoordinator::panic((reason), __FILE__, __LINE__)

enum class CommitPhase {
    Idle,
    Collecting,
    Arbitrating,
    ApplyingSemanticState,
    ApplyingTopology,
    ApplyingActivation,
    Reconciling
};

inline const char* toString(CommitPhase phase) {
    switch (phase) {
        case CommitPhase::Idle:                  return "Idle";
        case CommitPhase::Collecting:            return "Collecting";
        case CommitPhase::Arbitrating:           return "Arbitrating";
        case CommitPhase::ApplyingSemanticState: return "ApplyingSemanticState";
        case CommitPhase::ApplyingTopology:      return "ApplyingTopology";
        case CommitPhase::ApplyingActivation:    return "ApplyingActivation";
        case CommitPhase::Reconciling:           return "Reconciling";
    }
    return "Unknown";
}

struct FailureEvent {
    enum class Type {
        Divergence,
        Starvation,
        Quarantine,
        Rollback,
        EpochDesync,
        ActivationDenial
    };
    Type type;
    NodeId surfaceId;
    std::string details;
    uint64_t timestampMs;
};

class RollingFailureEventLog {
public:
    void log(FailureEvent::Type type, NodeId surfaceId, const std::string& details) {
        FailureEvent event;
        event.type = type;
        event.surfaceId = surfaceId;
        event.details = details;
        
        auto now = std::chrono::steady_clock::now();
        event.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        events_.push_back(event);
        if (events_.size() > 50) {
            events_.erase(events_.begin());
        }
    }
    const std::vector<FailureEvent>& events() const { return events_; }
    void clear() { events_.clear(); }
private:
    std::vector<FailureEvent> events_;
};

struct LatencyPercentiles {
    double p50 = 0.0;
    double p90 = 0.0;
    double p99 = 0.0;
    double worst = 0.0;
};

class LatencyTracker {
public:
    void record(double valMs) {
        history_[head_] = valMs;
        head_ = (head_ + 1) % kHistorySize;
        if (count_ < kHistorySize) count_++;
    }
    
    LatencyPercentiles compute() const {
        if (count_ == 0) return {};
        std::vector<double> sorted(history_.begin(), history_.begin() + count_);
        std::sort(sorted.begin(), sorted.end());
        
        LatencyPercentiles p;
        p.p50 = sorted[static_cast<size_t>(count_ * 0.50)];
        p.p90 = sorted[static_cast<size_t>(count_ * 0.90)];
        p.p99 = sorted[static_cast<size_t>(count_ * 0.99)];
        p.worst = sorted.back();
        return p;
    }
    void clear() { count_ = 0; head_ = 0; }
private:
    static constexpr size_t kHistorySize = 100;
    std::array<double, kHistorySize> history_;
    size_t head_ = 0;
    size_t count_ = 0;
};

struct RuntimeExecutionMetrics {
    double cycleDurationMs = 0.0;
    size_t totalIntentsEnqueued = 0;
    size_t coalescedCount = 0;
    size_t queueDepthBeforeCommit = 0;
    size_t queueDepthAfterCommit = 0;
    size_t maxDivergenceTicks = 0;
    double repairLatencyMs = 0.0;
    size_t budgetsExceeded = 0;

    // Phase 9B Systems Telemetry
    size_t cascadeCollapseCount = 0;
    size_t maxMutationFanout = 0;
    size_t maxEpochMutationCount = 0;
    size_t userHandlesCount = 0;
    size_t gdiHandlesCount = 0;
    size_t currentReentrancyDepth = 0;
    size_t maxObservedReentrancyDepth = 0;
    size_t suppressedRecursiveMessages = 0;
    size_t droppedIntents = 0;
    size_t rolloverCount = 0;
    size_t starvationEscalations = 0;
    size_t quarantinedSurfaces = 0;
    double frameJitterMs = 0.0;
    double commitJitterMs = 0.0;

    // DWM Throttling Observability
    double dwmLatencySpikeMs = 0.0;
    double framePacingDriftMs = 0.0;
    bool isOcclusionThrottled = false;
    bool isInvisibleWindowThrottled = false;
    int observedMonitorRefreshRate = 60;

    // User-Perceived Latency Metrics
    double focusAcquisitionLatencyMs = 0.0;
    double dragResponsivenessMs = 0.0;
    double workspaceSwitchPerceptionMs = 0.0;
    double keyboardNavigationDelayMs = 0.0;
};

struct CommitBudget {
    size_t maxOpsPerCycle = 15;
    double maxFrameTimeMs = 8.0; // Critical 7: 8.0ms budget to preserve 60fps pacing
};

class RuntimeCommitScheduler {
public:
    RuntimeCommitScheduler(RuntimeSceneState& sceneState, std::shared_ptr<RuntimeSurfaceRealizer> realizer, Compositor* compositor);
    ~RuntimeCommitScheduler() = default;

    Compositor* compositor() { return compositor_; }
    const Compositor* compositor() const { return compositor_; }

    // Subsystems enqueue mutation intents here
    void enqueueIntent(const RuntimeMutationIntent& intent);
    
    // Invalidate intents for a surface on destruction/rollback/recreation
    void invalidateSurface(NodeId surfaceId);

    // Invalidate intents for a workspace on destruction/rollback/recreation
    void invalidateWorkspace(WorkspaceId workspaceId);

    // Drives the execution tick
    void tick();

    // Queries
    RuntimeSceneState& sceneState() { return sceneState_; }
    const RuntimeSceneState& sceneState() const { return sceneState_; }
    CommitPhase currentPhase() const { return phase_; }
    const RuntimeExecutionMetrics& metrics() const { return metrics_; }
    RuntimeExecutionMetrics& mutableMetrics() { return metrics_; }
    const CommitBudget& budget() const { return budget_; }
    CommitBudget& mutableBudget() { return budget_; }
    
    void setTargetFPS(int fps) { targetFPS_ = fps; }

    // Phase 9C Health & Trace APIs
    const KernelHealth& health() const { return health_; }
    KernelHealth& mutableHealth() { return health_; }
    
    std::shared_ptr<KernelTrace> traceRecorder() { return traceRecorder_; }
    const std::shared_ptr<KernelTrace> traceRecorder() const { return traceRecorder_; }
    void setTraceRecorder(std::shared_ptr<KernelTrace> recorder) { traceRecorder_ = recorder; }
    void setRealizer(std::shared_ptr<RuntimeSurfaceRealizer> realizer) { realizer_ = realizer; }

    void freezeSchedulerEpoch() { epochFrozen_ = true; }
    bool isEpochFrozen() const { return epochFrozen_; }

    // Phase 9B APIs
    RollingFailureEventLog& eventLog() { return eventLog_; }
    const RollingFailureEventLog& eventLog() const { return eventLog_; }
    
    LatencyTracker& commitLatencyTracker() { return commitLatencyTracker_; }
    const LatencyTracker& commitLatencyTracker() const { return commitLatencyTracker_; }
    
    LatencyTracker& interactionLatencyTracker() { return interactionLatencyTracker_; }
    const LatencyTracker& interactionLatencyTracker() const { return interactionLatencyTracker_; }

    uint64_t getSurfaceEpoch(NodeId surfaceId) const {
        std::lock_guard<std::mutex> lock(queueMutex_);
        return queue_.getSurfaceEpoch(surfaceId);
    }

    // Time Dilation / Stutter Injection settings
    uint64_t commitPhaseArtificialDelayMs = 0;
    uint64_t realizationLatencyInjectionMs = 0;
    uint64_t reconciliationStallMs = 0;
    bool simulatePartialCommitFailure = false;

    void resetReplayIndex() { replayFrameIndex_ = 0; }
    size_t replayFrameIndex() const { return replayFrameIndex_; }

    RuntimeMutationQueue& queueForTesting() { return queue_; }

private:
    void executeCommitCycle();
    
    // Stages of the commit cycle
    void collectStage();
    void arbitrateStage();
    void applySemanticStage();
    void applyTopologyStage();
    void applyActivationStage();
    void reconcileStage();

    CommitPhase phase_ = CommitPhase::Idle;
    RuntimeSceneState& sceneState_;
    std::shared_ptr<RuntimeSurfaceRealizer> realizer_;
    Compositor* compositor_ = nullptr;
    
    RuntimeMutationQueue queue_;
    mutable std::mutex queueMutex_;
    std::vector<RuntimeMutationIntent> activeBatch_;
    
    CommitBudget budget_;
    RuntimeExecutionMetrics metrics_;
    int targetFPS_ = 60;

    // Phase 9B Fields
    RollingFailureEventLog eventLog_;
    LatencyTracker commitLatencyTracker_;
    LatencyTracker interactionLatencyTracker_;
    double lastCommitTimestampMs_ = 0.0;

    // Phase 9C Fields
    std::shared_ptr<KernelTrace> traceRecorder_;
    KernelHealth health_;
    std::unordered_map<NodeId, size_t> quarantineCount_;
    bool epochFrozen_ = false;
    size_t replayFrameIndex_ = 0;
    size_t prevTotalDivergenceTicks_ = 0;
};

} // namespace morphic
