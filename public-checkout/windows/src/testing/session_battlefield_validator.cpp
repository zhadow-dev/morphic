#include "session_battlefield_validator.h"
#include "../interaction/focus_graph.h"
#include "../composition/workspace_controller.h"
#include "../composition/workspace_intent.h"
#include "../experimental/attention_economics.h"
#include "../composition/workflow_graph.h"
#include "../composition/adaptive_orchestration.h"
#include "../composition/session_continuity.h"
#include <windows.h>

namespace morphic {

SessionBattlefieldValidator::SessionBattlefieldValidator(
    FocusGraph& graph, WorkspaceController& workspaceCtrl,
    WorkspaceIntentGraph& intents, AttentionBudget& budget,
    WorkflowGraph& workflows)
    : graph_(graph), workspaceCtrl_(workspaceCtrl),
      intents_(intents), budget_(budget), workflows_(workflows) {}

SessionBattlefieldValidator::ValidationReport SessionBattlefieldValidator::runFullValidation() {
    ValidationReport report;

    auto add = [&](TestResult r) {
        report.totalTests++;
        if (r.passed) report.passed++;
        else report.failed++;
        report.results.push_back(r);
    };

    add(testIntentPreservationAcrossSwitches());
    add(testAttentionBudgetDoesNotDegradeRuntime());
    add(testWorkflowGraphRemovalCoherence());
    add(testOrchestrationDoesNotMutateFocusGraph());
    add(testSessionRestorePolicySuggestions());
    add(testSemanticConfidenceIsolation());

    OutputDebugStringA(("SESSION_BATTLEFIELD: " + std::to_string(report.passed) +
        "/" + std::to_string(report.totalTests) + " tests passed\n").c_str());

    return report;
}

SessionBattlefieldValidator::TestResult
SessionBattlefieldValidator::testIntentPreservationAcrossSwitches() {
    TestResult r;
    r.testName = "IntentPreservationAcrossSwitches";

    WorkspaceId wsA = workspaceCtrl_.activeWorkspace();
    WorkspaceId wsB = workspaceCtrl_.createWorkspace();

    intents_.setIntent(wsA, OperationalActivity::Editing,
                       IntentDisposition::ContinuityCritical);
    intents_.setIntent(wsB, OperationalActivity::Debugging,
                       IntentDisposition::Transient);

    // Switch workspaces
    workspaceCtrl_.switchWorkspace(wsB);
    graph_.setActiveWorkspace(wsB.value);
    workspaceCtrl_.switchWorkspace(wsA);
    graph_.setActiveWorkspace(wsA.value);

    // Intent should be preserved
    auto intentA = intents_.getIntent(wsA);
    auto intentB = intents_.getIntent(wsB);

    if (intentA.activity != OperationalActivity::Editing ||
        intentA.disposition != IntentDisposition::ContinuityCritical) {
        r.failureReason = "Workspace A intent corrupted by switch";
    } else if (intentB.activity != OperationalActivity::Debugging ||
               intentB.disposition != IntentDisposition::Transient) {
        r.failureReason = "Workspace B intent corrupted by switch";
    } else {
        r.passed = true;
    }

    intents_.removeWorkspace(wsB);
    workspaceCtrl_.destroyWorkspace(wsB);
    return r;
}

SessionBattlefieldValidator::TestResult
SessionBattlefieldValidator::testAttentionBudgetDoesNotDegradeRuntime() {
    TestResult r;
    r.testName = "AttentionBudgetDoesNotDegradeRuntime";

    // Create surfaces
    FocusNode node;
    node.id = 60001;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(node.id, 1);

    // Set budget extremely low so it's over budget
    budget_.setCapacity(1.0);
    budget_.setSurfaceAttention(node.id, AttentionLevel::Background,
                                AttentionCost{10.0, 10.0, 10.0});

    bool overBudget = budget_.isOverBudget();
    if (!overBudget) {
        r.failureReason = "Budget should be over limit";
        graph_.unregisterNode(node.id);
        return r;
    }

    // Despite being over budget, runtime state must be unaffected
    auto checkpoint = graph_.createCheckpoint();
    if (checkpoint.semanticFocus != node.id) {
        r.failureReason = "Focus changed despite attention budget being advisory only";
    } else if (!checkpoint.valid) {
        r.failureReason = "Checkpoint invalid despite attention budget being advisory only";
    } else {
        r.passed = true;
    }

    budget_.removeSurface(node.id);
    budget_.setCapacity(100.0);
    graph_.unregisterNode(node.id);
    return r;
}

SessionBattlefieldValidator::TestResult
SessionBattlefieldValidator::testWorkflowGraphRemovalCoherence() {
    TestResult r;
    r.testName = "WorkflowGraphRemovalCoherence";

    FocusNode nodeA, nodeB, nodeC;
    nodeA.id = 60010; nodeA.domain = FocusDomain::Workspace;
    nodeA.behavior = AttentionBehavior::Interactive;
    nodeA.currentEligibility = FocusEligibility::Eligible;
    nodeA.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    nodeB = nodeA; nodeB.id = 60011;
    nodeC = nodeA; nodeC.id = 60012;

    uint64_t ws = workspaceCtrl_.activeWorkspace().value;
    graph_.registerNode(nodeA, ws);
    graph_.registerNode(nodeB, ws);
    graph_.registerNode(nodeC, ws);

    workflows_.addRelationship(nodeA.id, nodeB.id, WorkflowRelationship::CoEditing);
    workflows_.addRelationship(nodeB.id, nodeC.id, WorkflowRelationship::InspectedBy);

    // Remove nodeB from workflow graph
    workflows_.removeSurface(nodeB.id);

    // Workflow peers of A should be empty now (B was removed)
    auto peersA = workflows_.workflowPeers(nodeA.id);
    if (!peersA.empty()) {
        r.failureReason = "Stale workflow edges remain after surface removal";
    } else {
        r.passed = true;
    }

    // Runtime graph should be completely unaffected
    auto na = graph_.getNode(nodeA.id);
    auto nb = graph_.getNode(nodeB.id);
    auto nc = graph_.getNode(nodeC.id);
    if (!na.has_value() || !nb.has_value() || !nc.has_value()) {
        r.failureReason = "Workflow removal affected runtime focus graph";
        r.passed = false;
    }

    graph_.unregisterNode(nodeA.id);
    graph_.unregisterNode(nodeB.id);
    graph_.unregisterNode(nodeC.id);
    return r;
}

SessionBattlefieldValidator::TestResult
SessionBattlefieldValidator::testOrchestrationDoesNotMutateFocusGraph() {
    TestResult r;
    r.testName = "OrchestrationDoesNotMutateFocusGraph";

    FocusNode node;
    node.id = 60020;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(node.id, 1);

    // Capture state before orchestration
    AttentionCheckpoint before = graph_.createCheckpoint();

    // Generate orchestration hints
    AdaptiveOrchestrator orchestrator;
    SessionState session;
    session.interruption.reason = InterruptionReason::CrashRecovery;
    auto hints = orchestrator.suggestRestoreOrder(session, intents_);

    budget_.setSurfaceAttention(node.id, AttentionLevel::Background);
    budget_.setCapacity(0.1); // Force over-budget
    auto degradeHints = orchestrator.suggestDegradationPreference(budget_);

    // Capture state after orchestration — must be IDENTICAL
    AttentionCheckpoint after = graph_.createCheckpoint();

    if (before.semanticFocus != after.semanticFocus) {
        r.failureReason = "Focus graph mutated by orchestration hints";
    } else if (before.valid != after.valid) {
        r.failureReason = "Checkpoint validity changed by orchestration hints";
    } else {
        r.passed = true;
    }

    budget_.removeSurface(node.id);
    budget_.setCapacity(100.0);
    graph_.unregisterNode(node.id);
    return r;
}

SessionBattlefieldValidator::TestResult
SessionBattlefieldValidator::testSessionRestorePolicySuggestions() {
    TestResult r;
    r.testName = "SessionRestorePolicySuggestions";

    // Verify each interruption reason maps to a sensible restore policy
    auto crashPolicy = SessionState::suggestPolicyForInterruption(
        InterruptionReason::CrashRecovery);
    auto pausePolicy = SessionState::suggestPolicyForInterruption(
        InterruptionReason::IntentionalPause);
    auto endPolicy = SessionState::suggestPolicyForInterruption(
        InterruptionReason::SessionEnd);

    if (crashPolicy != SessionRestorePolicy::RestoreMinimal) {
        r.failureReason = "Crash recovery should suggest RestoreMinimal";
    } else if (pausePolicy != SessionRestorePolicy::RestoreFullContext) {
        r.failureReason = "Intentional pause should suggest RestoreFullContext";
    } else if (endPolicy != SessionRestorePolicy::RestoreTopologyOnly) {
        r.failureReason = "Session end should suggest RestoreTopologyOnly";
    } else {
        r.passed = true;
    }

    return r;
}

SessionBattlefieldValidator::TestResult
SessionBattlefieldValidator::testSemanticConfidenceIsolation() {
    TestResult r;
    r.testName = "SemanticConfidenceIsolation";

    WorkspaceId ws = workspaceCtrl_.activeWorkspace();

    // Set intent with weak confidence
    intents_.setIntent(ws, OperationalActivity::Monitoring,
                       IntentDisposition::Transient,
                       SemanticConfidence::WeaklyInferred);

    // Runtime focus graph should not care about confidence
    FocusNode node;
    node.id = 60030;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, ws.value);
    graph_.commitRealizedActivation(node.id, 1);

    auto cp = graph_.createCheckpoint();
    if (cp.semanticFocus != node.id) {
        r.failureReason = "WeaklyInferred confidence affected runtime focus";
    } else {
        r.passed = true;
    }

    intents_.removeWorkspace(ws);
    graph_.unregisterNode(node.id);
    return r;
}

} // namespace morphic
