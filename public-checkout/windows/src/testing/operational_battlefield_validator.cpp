#include "operational_battlefield_validator.h"
#include "../interaction/focus_graph.h"
#include "../composition/workspace_controller.h"
#include <windows.h>
#include <algorithm>

namespace morphic {

OperationalBattlefieldValidator::OperationalBattlefieldValidator(
    FocusGraph& graph, WorkspaceController& workspaceCtrl, RuntimePressureEvaluator& pressure)
    : graph_(graph), workspaceCtrl_(workspaceCtrl), pressure_(pressure) {}

NodeId OperationalBattlefieldValidator::createBattleSurface(uint64_t workspaceKey) {
    NodeId id = nextId_++;
    FocusNode node;
    node.id = id;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, workspaceKey);
    battleSurfaces_.push_back(id);
    return id;
}

void OperationalBattlefieldValidator::destroyBattleSurface(NodeId id) {
    graph_.unregisterNode(id);
    battleSurfaces_.erase(std::remove(battleSurfaces_.begin(), battleSurfaces_.end(), id),
                          battleSurfaces_.end());
}

void OperationalBattlefieldValidator::cleanupBattle() {
    for (NodeId id : battleSurfaces_) {
        graph_.unregisterNode(id);
    }
    battleSurfaces_.clear();
    pressure_.resetWindow();
}

OperationalBattlefieldValidator::BattleReport OperationalBattlefieldValidator::runFullBattle() {
    BattleReport report;

    auto add = [&](BattleResult r) {
        report.totalScenarios++;
        if (r.survived) report.survived++;
        else report.failed++;
        report.results.push_back(r);
    };

    add(scenarioSchedulerFlood());
    add(scenarioMutationDrift());
    add(scenarioDivergenceStormUnderPressure());
    add(scenarioRendererCrashCascade());
    add(scenarioRecoveryOnlyDegradedMode());
    add(scenarioCrossWorkspaceFaultIsolation());
    add(scenarioTransactionStarvation());
    add(scenarioMassiveTopologyChurn());
    add(scenarioPressureEscalationLadder());
    add(scenarioSustainedFailureSurvival());

    OutputDebugStringA(("BATTLEFIELD: " + std::to_string(report.survived) +
        "/" + std::to_string(report.totalScenarios) + " scenarios survived\n").c_str());

    return report;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioSchedulerFlood() {
    BattleResult r;
    r.scenarioName = "SchedulerFlood";
    cleanupBattle();

    NodeId s = createBattleSurface(workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(s, 1);

    for (int i = 0; i < 5000; i++) {
        graph_.evaluateTransition(InteractionIntent::CycleForward, FocusInitiator::UserInput);
        pressure_.recordTransactionCommit();
    }

    auto cp = graph_.createCheckpoint();
    r.semanticIntegrity = cp.valid;
    r.survived = cp.valid;

    cleanupBattle();
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioMutationDrift() {
    BattleResult r;
    r.scenarioName = "MutationDrift";
    cleanupBattle();

    // 1000 create/destroy cycles
    for (int i = 0; i < 1000; i++) {
        NodeId s = createBattleSurface(workspaceCtrl_.activeWorkspace().value);
        if (i % 3 == 0) {
            destroyBattleSurface(s);
        }
    }

    // Verify no stale references
    const auto& chain = graph_.workspaceChain(workspaceCtrl_.activeWorkspace().value);
    for (NodeId id : chain) {
        if (!graph_.getNode(id).has_value()) {
            r.failureReason = "Stale node in chain after mutation drift";
            r.semanticIntegrity = false;
            cleanupBattle();
            return r;
        }
    }

    r.survived = true;
    cleanupBattle();
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioDivergenceStormUnderPressure() {
    BattleResult r;
    r.scenarioName = "DivergenceStormUnderPressure";
    cleanupBattle();

    NodeId s = createBattleSurface(workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(s, 1);

    // Saturate pressure
    for (int i = 0; i < 100; i++) {
        pressure_.recordActivationAttempt(false);
        pressure_.recordTransactionRollback();
    }

    // Fire divergence storm
    DivergenceContext ctx;
    for (int i = 0; i < 20; i++) {
        graph_.resolveDivergence(FocusDivergence::OSDeniedActivation, s, ctx);
        if (ctx.retryCount > 3) ctx = DivergenceContext{};
    }

    // Graph should still function
    auto cp = graph_.createCheckpoint();
    r.semanticIntegrity = cp.valid;
    r.survived = cp.valid;

    cleanupBattle();
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioRendererCrashCascade() {
    BattleResult r;
    r.scenarioName = "RendererCrashCascade";
    cleanupBattle();

    // Create surfaces across 3 workspaces
    uint64_t ws1 = workspaceCtrl_.activeWorkspace().value;
    WorkspaceId ws2 = workspaceCtrl_.createWorkspace();
    WorkspaceId ws3 = workspaceCtrl_.createWorkspace();

    NodeId s1 = createBattleSurface(ws1);
    NodeId s2 = createBattleSurface(ws2.value);
    NodeId s3 = createBattleSurface(ws3.value);
    (void)s2; (void)s3;

    graph_.commitRealizedActivation(s1, 1);

    // "Crash" renderer for s1 — invalidate it
    graph_.invalidateSemanticReferences(s1);

    // Verify other workspace surfaces are unaffected
    auto n2 = graph_.getNode(s2);
    auto n3 = graph_.getNode(s3);
    if (!n2.has_value() || !n3.has_value()) {
        r.failureReason = "Cross-workspace surfaces destroyed by renderer crash";
        r.boundedFailure = false;
    } else {
        r.survived = true;
        r.boundedFailure = true;
    }

    cleanupBattle();
    workspaceCtrl_.destroyWorkspace(ws2);
    workspaceCtrl_.destroyWorkspace(ws3);
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioRecoveryOnlyDegradedMode() {
    BattleResult r;
    r.scenarioName = "RecoveryOnlyDegradedMode";
    cleanupBattle();

    // Simulate critical pressure
    RuntimePressureSnapshot snap;
    snap.globalState = RuntimePressureState::RecoveryOnly;

    RuntimeDegradedMode mode = degradedPolicy_.evaluateMode(snap.globalState);

    r.survived = degradedPolicy_.isSemanticPreservationOnly(mode);
    r.operationalSurvivability = !degradedPolicy_.isDetachedOperationPermitted(mode);

    cleanupBattle();
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioCrossWorkspaceFaultIsolation() {
    BattleResult r;
    r.scenarioName = "CrossWorkspaceFaultIsolation";
    cleanupBattle();

    WorkspaceId wsA = workspaceCtrl_.activeWorkspace();
    WorkspaceId wsB = workspaceCtrl_.createWorkspace();

    NodeId sA = createBattleSurface(wsA.value);
    NodeId sB = createBattleSurface(wsB.value);

    graph_.commitRealizedActivation(sA, 1);

    // Destroy workspace A surface
    destroyBattleSurface(sA);

    // Workspace B should be completely unaffected
    auto bNode = graph_.getNode(sB);
    if (!bNode.has_value() || bNode->continuity.state != ContinuityState::Coherent) {
        r.failureReason = "Workspace B affected by workspace A fault";
        r.boundedFailure = false;
    } else {
        r.survived = true;
    }

    cleanupBattle();
    workspaceCtrl_.destroyWorkspace(wsB);
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioTransactionStarvation() {
    BattleResult r;
    r.scenarioName = "TransactionStarvation";
    cleanupBattle();

    NodeId s = createBattleSurface(workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(s, 1);

    // 1000 evaluations without any commit — simulates starvation
    for (int i = 0; i < 1000; i++) {
        graph_.evaluateTransition(InteractionIntent::CycleForward, FocusInitiator::UserInput);
    }

    // Focus should NOT have drifted (evaluateTransition doesn't commit)
    auto cp = graph_.createCheckpoint();
    r.survived = (cp.semanticFocus == s);
    r.schedulerIntegrity = true;

    cleanupBattle();
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioMassiveTopologyChurn() {
    BattleResult r;
    r.scenarioName = "MassiveTopologyChurn";
    cleanupBattle();

    // 200 workspace create/destroy cycles with surfaces
    for (int i = 0; i < 200; i++) {
        WorkspaceId ws = workspaceCtrl_.createWorkspace();
        NodeId s = createBattleSurface(ws.value);
        destroyBattleSurface(s);
        workspaceCtrl_.destroyWorkspace(ws);
        pressure_.recordTopologyMutation();
    }

    // Default workspace must survive
    r.survived = workspaceCtrl_.workspaceExists(WorkspaceId::defaultId());
    if (!r.survived) {
        r.failureReason = "Default workspace destroyed by topology churn";
    }

    cleanupBattle();
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioPressureEscalationLadder() {
    BattleResult r;
    r.scenarioName = "PressureEscalationLadder";
    cleanupBattle();

    // Walk the entire pressure ladder
    RuntimePressureState states[] = {
        RuntimePressureState::Stable,
        RuntimePressureState::Constrained,
        RuntimePressureState::Degraded,
        RuntimePressureState::Critical,
        RuntimePressureState::RecoveryOnly
    };

    for (auto state : states) {
        RuntimeDegradedMode mode = degradedPolicy_.evaluateMode(state);
        // Each state should produce a valid degraded mode
        if (state == RuntimePressureState::RecoveryOnly &&
            mode != RuntimeDegradedMode::RecoveryOnly) {
            r.failureReason = "RecoveryOnly pressure did not produce RecoveryOnly mode";
            cleanupBattle();
            return r;
        }
    }

    r.survived = true;
    cleanupBattle();
    return r;
}

OperationalBattlefieldValidator::BattleResult OperationalBattlefieldValidator::scenarioSustainedFailureSurvival() {
    BattleResult r;
    r.scenarioName = "SustainedFailureSurvival";
    cleanupBattle();

    NodeId s = createBattleSurface(workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(s, 1);

    // 500 cycles of: divergence → rollback → restore → create → destroy
    for (int i = 0; i < 500; i++) {
        DivergenceContext ctx;
        graph_.resolveDivergence(FocusDivergence::OSDeniedActivation, s, ctx);

        AttentionCheckpoint cp = graph_.createCheckpoint();
        graph_.evaluateCheckpointRestore(cp);

        NodeId temp = createBattleSurface(workspaceCtrl_.activeWorkspace().value);
        destroyBattleSurface(temp);

        pressure_.recordActivationAttempt(false);
        pressure_.recordTransactionRollback();
    }

    // Runtime should still be operational
    auto finalCp = graph_.createCheckpoint();
    r.semanticIntegrity = finalCp.valid;
    r.survived = finalCp.valid;

    cleanupBattle();
    return r;
}

} // namespace morphic
