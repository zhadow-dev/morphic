#pragma once

#include <cstdint>
#include <algorithm>

namespace morphic {

// Phase 6A: Runtime Pressure Domains.
//
// Operational survivability pressure — separate from semantic correctness.
// Pressure affects timing, throttling, prioritization, degradation.
// Pressure NEVER affects semantic truth, arbitration truth, or continuity authority.

enum class RuntimePressureState {
    Stable,         // All systems nominal
    Constrained,    // Some pressure accumulating, recoverable
    Degraded,       // Significant pressure, degraded-mode may activate
    Critical,       // Near-failure, aggressive throttling required
    RecoveryOnly    // Only semantic preservation operations permitted
};

enum class RuntimePressureDomain {
    Scheduler,
    Renderer,
    Memory,
    Activation,
    Transaction,
    Continuity,
    Topology
};

// Aggregated pressure snapshot — operational observability.
struct RuntimePressureSnapshot {
    RuntimePressureState globalState = RuntimePressureState::Stable;

    double schedulerLoad = 0.0;              // 0.0 = idle, 1.0 = saturated
    double transactionBacklog = 0.0;         // Pending transaction count / capacity
    double rendererRecoveryPressure = 0.0;   // Fraction of renderers in recovery
    double activationFailureRate = 0.0;      // Recent activation denials / total attempts
    double continuityFractureRate = 0.0;     // Fractured nodes / total nodes
    double topologyMutationRate = 0.0;       // Mutations per second (normalized)

    bool degradedModeActive = false;
};

// Phase 6A: Runtime Pressure Evaluator.
//
// Consumes runtime metrics and emits bounded RuntimePressureState.
// Called periodically (e.g., once per frame or once per N frames).
class RuntimePressureEvaluator {
public:
    RuntimePressureEvaluator() = default;

    // --- Input: Record operational events ---

    void recordActivationAttempt(bool succeeded) {
        totalActivations_++;
        if (!succeeded) failedActivations_++;
    }

    void recordTransactionCommit() { committedTransactions_++; }
    void recordTransactionRollback() { rolledBackTransactions_++; }

    void recordDivergence() { divergenceCount_++; }
    void recordContinuityFracture() { fractureCount_++; }
    void recordContinuityRepair() { repairCount_++; }
    void recordTopologyMutation() { topologyMutations_++; }

    void setRendererRecoveryCount(int recovering, int total) {
        recoveringRenderers_ = recovering;
        totalRenderers_ = total;
    }

    void setSchedulerQueueDepth(int depth, int capacity) {
        schedulerQueueDepth_ = depth;
        schedulerCapacity_ = capacity;
    }

    // --- Output: Evaluate pressure state ---

    RuntimePressureSnapshot evaluate() {
        RuntimePressureSnapshot snap;

        // Scheduler load
        snap.schedulerLoad = (schedulerCapacity_ > 0)
            ? static_cast<double>(schedulerQueueDepth_) / schedulerCapacity_
            : 0.0;

        // Transaction backlog
        int totalTx = committedTransactions_ + rolledBackTransactions_;
        snap.transactionBacklog = (totalTx > 0)
            ? static_cast<double>(rolledBackTransactions_) / totalTx
            : 0.0;

        // Renderer recovery pressure
        snap.rendererRecoveryPressure = (totalRenderers_ > 0)
            ? static_cast<double>(recoveringRenderers_) / totalRenderers_
            : 0.0;

        // Activation failure rate
        snap.activationFailureRate = (totalActivations_ > 0)
            ? static_cast<double>(failedActivations_) / totalActivations_
            : 0.0;

        // Continuity fracture rate
        int totalContinuity = fractureCount_ + repairCount_;
        snap.continuityFractureRate = (totalContinuity > 0)
            ? static_cast<double>(fractureCount_) / totalContinuity
            : 0.0;

        // Topology mutation rate (raw count, normalized by caller)
        snap.topologyMutationRate = static_cast<double>(topologyMutations_);

        // Global state classification
        double maxPressure = (std::max)({
            snap.schedulerLoad,
            snap.transactionBacklog,
            snap.rendererRecoveryPressure,
            snap.activationFailureRate,
            snap.continuityFractureRate
        });

        if (maxPressure < 0.25) {
            snap.globalState = RuntimePressureState::Stable;
        } else if (maxPressure < 0.50) {
            snap.globalState = RuntimePressureState::Constrained;
        } else if (maxPressure < 0.75) {
            snap.globalState = RuntimePressureState::Degraded;
        } else if (maxPressure < 0.90) {
            snap.globalState = RuntimePressureState::Critical;
        } else {
            snap.globalState = RuntimePressureState::RecoveryOnly;
        }

        lastSnapshot_ = snap;
        return snap;
    }

    const RuntimePressureSnapshot& lastSnapshot() const { return lastSnapshot_; }

    // Reset counters for next evaluation window
    void resetWindow() {
        totalActivations_ = 0;
        failedActivations_ = 0;
        committedTransactions_ = 0;
        rolledBackTransactions_ = 0;
        divergenceCount_ = 0;
        fractureCount_ = 0;
        repairCount_ = 0;
        topologyMutations_ = 0;
    }

private:
    // Counters (per evaluation window)
    int totalActivations_ = 0;
    int failedActivations_ = 0;
    int committedTransactions_ = 0;
    int rolledBackTransactions_ = 0;
    int divergenceCount_ = 0;
    int fractureCount_ = 0;
    int repairCount_ = 0;
    int topologyMutations_ = 0;

    // Renderer state (set externally)
    int recoveringRenderers_ = 0;
    int totalRenderers_ = 0;

    // Scheduler state (set externally)
    int schedulerQueueDepth_ = 0;
    int schedulerCapacity_ = 100;

    RuntimePressureSnapshot lastSnapshot_;
};

} // namespace morphic
