#pragma once

#include "../core/types.h"
#include <string>
#include <vector>

namespace morphic {

class FocusGraph;
class WorkspaceController;

// Phase 6C: Long-Horizon Drift Detection.
//
// Tracks SEMANTIC drift — stale references, mutation residue, checkpoint decay.
// NOT spatial/transform drift (that's DriftDetector from Phase 1B).
//
// Drift score must converge toward stability over time,
// not accumulate toward corruption.

struct RuntimeDriftMetrics {
    uint64_t staleCheckpointCount = 0;
    uint64_t lineageMismatchRecoveries = 0;
    uint64_t rollbackResidueCount = 0;
    uint64_t deferredMutationResidue = 0;
    uint64_t continuityRepairCount = 0;

    // 0.0 = perfect stability, 1.0 = severe drift
    double semanticDriftScore = 0.0;
};

class RuntimeDriftValidator {
public:
    struct DriftSample {
        int iteration;
        RuntimeDriftMetrics metrics;
    };

    struct ValidationReport {
        bool convergent = false;
        double initialDriftScore = 0.0;
        double finalDriftScore = 0.0;
        int totalIterations = 0;
        std::vector<DriftSample> samples;
        std::string failureReason;
    };

    RuntimeDriftValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl);

    // Run continuous mutations for N iterations.
    // Samples drift metrics periodically.
    // Returns whether drift score converges toward stability.
    ValidationReport runDriftValidation(int iterations);

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;

    // Mutation operations
    void performMutationCycle(int cycle);
    RuntimeDriftMetrics measureDrift();

    NodeId nextId_ = 40000;
    std::vector<NodeId> activeSurfaces_;
    uint64_t staleCheckpoints_ = 0;
    uint64_t rollbackResidues_ = 0;
    uint64_t continuityRepairs_ = 0;
};

} // namespace morphic
