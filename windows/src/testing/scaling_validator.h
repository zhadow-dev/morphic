#pragma once

#include "../core/types.h"
#include "../interaction/focus_types.h"
#include <string>
#include <vector>
#include <functional>

namespace morphic {

class FocusGraph;
class WorkspaceController;
class Compositor;

// Phase 5C: Scaling & Temporal Corruption Validation.
//
// Validates semantic stability under topology scale. NOT performance
// benchmarking — semantic coherence testing.
//
// Two categories:
// 1. Structural integrity — graph correctness after mutations
// 2. Temporal corruption — coherence under rapid oscillation and concurrent events
class ScalingValidator {
public:
    struct TestResult {
        std::string testName;
        bool passed = false;
        std::string failureReason;
    };

    struct ValidationReport {
        int totalTests = 0;
        int passed = 0;
        int failed = 0;
        std::vector<TestResult> results;

        bool allPassed() const { return failed == 0; }
    };

    ScalingValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl);

    // Run all validation suites at the given surface count
    ValidationReport runFullValidation(int surfaceCount);

    // --- Structural Integrity Tests ---

    // No orphaned checkpoints, no stale chain references after mutations
    TestResult testContinuityCoherence(int surfaceCount);

    // Checkpoint → destroy random surface → restore → verify focus lands correctly
    TestResult testRestoreCorrectness(int surfaceCount);

    // Begin tx → stage 3 intents → rollback → verify graph unchanged
    TestResult testTransactionIntegrity(int surfaceCount);

    // Modal on workspace A → switch to workspace B → verify suppression doesn't leak
    TestResult testModalSuppressionIsolation();

    // Inject OSDeniedActivation → verify fallback → verify no infinite loop
    TestResult testDivergenceRecovery(int surfaceCount);

    // --- Temporal Corruption Tests ---

    // Switch A→B→A→C→A in rapid succession
    TestResult testRapidWorkspaceOscillation();

    // Rollback during divergence storm
    TestResult testRollbackDuringDivergenceStorm();

    // Destroy surface during ContinuityState::Reconstructing
    TestResult testDestroyDuringReconstruction();

    // Detached recovery while modal active on a different workspace
    TestResult testDetachedRecoveryWithActiveModal();

    // Checkpoint invalidation under lineage mutation (generation mismatch)
    TestResult testCheckpointLineageMismatch();

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;

    // Helpers
    NodeId createTestSurface(int index, uint64_t workspaceKey);
    void destroyTestSurface(NodeId id);
    void destroyAllTestSurfaces();

    std::vector<NodeId> testSurfaces_;
    NodeId nextTestId_ = 10000; // High range to avoid collisions
};

} // namespace morphic
