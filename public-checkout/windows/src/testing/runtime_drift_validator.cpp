#include "runtime_drift_validator.h"
#include "../interaction/focus_graph.h"
#include "../composition/workspace_controller.h"
#include <windows.h>
#include <algorithm>
#include <cmath>

namespace morphic {

RuntimeDriftValidator::RuntimeDriftValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl)
    : graph_(graph), workspaceCtrl_(workspaceCtrl) {}

void RuntimeDriftValidator::performMutationCycle(int cycle) {
    // Mix of mutations that exercise every semantic subsystem
    int op = cycle % 7;

    switch (op) {
        case 0: {
            // Create surface
            FocusNode node;
            node.id = nextId_++;
            node.domain = FocusDomain::Workspace;
            node.behavior = AttentionBehavior::Interactive;
            node.currentEligibility = FocusEligibility::Eligible;
            node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
            graph_.registerNode(node, workspaceCtrl_.activeWorkspace().value);
            activeSurfaces_.push_back(node.id);
            break;
        }
        case 1: {
            // Destroy random surface
            if (!activeSurfaces_.empty()) {
                NodeId target = activeSurfaces_.back();
                graph_.unregisterNode(target);
                activeSurfaces_.pop_back();
            }
            break;
        }
        case 2: {
            // Workspace switch
            if (workspaceCtrl_.workspaceCount() < 4) {
                workspaceCtrl_.createWorkspace();
            }
            // Switch to a non-default workspace and back
            WorkspaceId ws;
            ws.value = 2;
            if (workspaceCtrl_.workspaceExists(ws)) {
                workspaceCtrl_.switchWorkspace(ws);
                graph_.setActiveWorkspace(ws.value);
                workspaceCtrl_.switchWorkspace(WorkspaceId::defaultId());
                graph_.setActiveWorkspace(1);
            }
            break;
        }
        case 3: {
            // Checkpoint and restore
            AttentionCheckpoint cp = graph_.createCheckpoint();
            if (!activeSurfaces_.empty()) {
                graph_.evaluateTransition(InteractionIntent::CycleForward, FocusInitiator::UserInput);
            }
            FocusDecision d = graph_.evaluateCheckpointRestore(cp); (void)d;
            staleCheckpoints_++;
            break;
        }
        case 4: {
            // Rollback cycle
            AttentionCheckpoint before = graph_.createCheckpoint();
            graph_.evaluateTransition(InteractionIntent::CycleForward, FocusInitiator::UserInput);
            graph_.evaluateCheckpointRestore(before);
            rollbackResidues_++;
            break;
        }
        case 5: {
            // Divergence + recovery
            if (!activeSurfaces_.empty()) {
                DivergenceContext ctx;
                graph_.resolveDivergence(FocusDivergence::OSDeniedActivation, activeSurfaces_[0], ctx);
                continuityRepairs_++;
            }
            break;
        }
        case 6: {
            // Modal push/pop churn
            if (!activeSurfaces_.empty()) {
                ModalSuppressionPolicy modal;
                modal.modalNodeId = activeSurfaces_.back();
                modal.blocksWorkspace = true;
                modal.blocksDetached = false;
                modal.allowClickThrough = false;
                graph_.pushModalSuppression(modal);
                graph_.popModalSuppression(activeSurfaces_.back());
            }
            break;
        }
    }
}

RuntimeDriftMetrics RuntimeDriftValidator::measureDrift() {
    RuntimeDriftMetrics m;
    m.staleCheckpointCount = staleCheckpoints_;
    m.rollbackResidueCount = rollbackResidues_;
    m.continuityRepairCount = continuityRepairs_;

    // Check for stale references in workspace chains
    const auto& chain = graph_.workspaceChain(workspaceCtrl_.activeWorkspace().value);
    uint64_t staleRefs = 0;
    for (NodeId id : chain) {
        if (!graph_.getNode(id).has_value()) {
            staleRefs++;
        }
    }
    m.deferredMutationResidue = staleRefs;

    // Compute drift score: weighted sum of residue indicators
    // Stale references are the most dangerous
    double staleWeight = (staleRefs > 0) ? 0.5 : 0.0;
    double rollbackWeight = (rollbackResidues_ > 100) ? 0.2 : 0.0;
    double repairWeight = (continuityRepairs_ > 50) ? 0.15 : 0.0;
    double checkpointWeight = (staleCheckpoints_ > 200) ? 0.15 : 0.0;

    m.semanticDriftScore = staleWeight + rollbackWeight + repairWeight + checkpointWeight;
    m.semanticDriftScore = (std::min)(m.semanticDriftScore, 1.0);

    return m;
}

RuntimeDriftValidator::ValidationReport RuntimeDriftValidator::runDriftValidation(int iterations) {
    ValidationReport report;
    report.totalIterations = iterations;

    int sampleInterval = (std::max)(iterations / 10, 1);

    for (int i = 0; i < iterations; i++) {
        performMutationCycle(i);

        if (i % sampleInterval == 0 || i == iterations - 1) {
            DriftSample sample;
            sample.iteration = i;
            sample.metrics = measureDrift();
            report.samples.push_back(sample);

            if (i == 0) {
                report.initialDriftScore = sample.metrics.semanticDriftScore;
            }
        }
    }

    if (!report.samples.empty()) {
        report.finalDriftScore = report.samples.back().metrics.semanticDriftScore;
    }

    // Convergence check: drift score should NOT be growing
    // Compare first half average to second half average
    if (report.samples.size() >= 4) {
        size_t mid = report.samples.size() / 2;
        double firstHalfAvg = 0.0;
        double secondHalfAvg = 0.0;
        for (size_t i = 0; i < mid; i++) {
            firstHalfAvg += report.samples[i].metrics.semanticDriftScore;
        }
        for (size_t i = mid; i < report.samples.size(); i++) {
            secondHalfAvg += report.samples[i].metrics.semanticDriftScore;
        }
        firstHalfAvg /= mid;
        secondHalfAvg /= (report.samples.size() - mid);

        // Convergent if second half is not significantly worse than first half
        report.convergent = (secondHalfAvg <= firstHalfAvg + 0.1);
        if (!report.convergent) {
            report.failureReason = "Drift score diverging: first_half=" +
                std::to_string(firstHalfAvg) + " second_half=" +
                std::to_string(secondHalfAvg);
        }
    } else {
        report.convergent = true;
    }

    // Cleanup
    for (NodeId id : activeSurfaces_) {
        graph_.unregisterNode(id);
    }
    activeSurfaces_.clear();

    OutputDebugStringA(("DRIFT_VALIDATOR: " + std::to_string(iterations) +
        " iterations, drift=" + std::to_string(report.finalDriftScore) +
        " convergent=" + (report.convergent ? "yes" : "NO") + "\n").c_str());

    return report;
}

} // namespace morphic
