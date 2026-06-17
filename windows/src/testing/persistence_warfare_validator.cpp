#include "persistence_warfare_validator.h"
#include "../interaction/focus_graph.h"
#include "../composition/workspace_controller.h"
#include "../composition/workspace_intent.h"
#include "../experimental/attention_economics.h"
#include "../composition/workflow_graph.h"
#include <windows.h>

namespace morphic {

PersistenceWarfareValidator::PersistenceWarfareValidator(
    FocusGraph& graph, WorkspaceController& workspaceCtrl,
    WorkspaceIntentGraph& intents, AttentionBudget& budget,
    WorkflowGraph& workflows)
    : graph_(graph), workspaceCtrl_(workspaceCtrl),
      intents_(intents), budget_(budget), workflows_(workflows) {}

PersistenceWarfareValidator::ValidationReport PersistenceWarfareValidator::runFullValidation() {
    ValidationReport report;

    auto add = [&](TestResult r) {
        report.totalTests++;
        if (r.passed) report.passed++;
        else report.failed++;
        report.results.push_back(r);
    };

    add(testCrashMidWrite());
    add(testStaleSchemaRejection());
    add(testOrphanTopologyRepair());
    add(testWorkflowCorruptionRecovery());
    add(testPartialMinimalRestore());
    add(testFullRoundTripWithCorruption());

    OutputDebugStringA(("PERSISTENCE_WARFARE: " + std::to_string(report.passed) +
        "/" + std::to_string(report.totalTests) + " tests passed\n").c_str());

    return report;
}

PersistenceWarfareValidator::TestResult PersistenceWarfareValidator::testCrashMidWrite() {
    TestResult r;
    r.testName = "CrashMidWrite";

    // Simulate: capture session but truncate the result (crash mid-write)
    SessionState session;
    session.interruption.reason = InterruptionReason::CrashRecovery;

    PersistedSession persisted = persistence_.captureSession(session, intents_, workflows_, budget_);

    // Simulate truncation: clear workflow edges (partial write)
    persisted.workflowEdges.clear();
    // But leave workspace intents intact

    // Diagnose should detect clean (no corrupt edges, just empty)
    auto corruption = persistence_.diagnoseCorruption(persisted);

    // Should be restorable in best-effort mode
    bool canRestore = persistence_.canRestore(corruption, PartialRestoreMode::RestoreWhatIsAvailable);
    if (!canRestore) {
        r.failureReason = "Cannot restore truncated session in best-effort mode";
    } else {
        r.passed = true;
    }

    return r;
}

PersistenceWarfareValidator::TestResult PersistenceWarfareValidator::testStaleSchemaRejection() {
    TestResult r;
    r.testName = "StaleSchemaRejection";

    PersistedSession persisted;
    persisted.schemaVersion = 999; // Future/invalid version

    auto corruption = persistence_.diagnoseCorruption(persisted);

    if (corruption.isClean) {
        r.failureReason = "Stale schema not detected as corruption";
    } else {
        r.passed = true;
    }

    return r;
}

PersistenceWarfareValidator::TestResult PersistenceWarfareValidator::testOrphanTopologyRepair() {
    TestResult r;
    r.testName = "OrphanTopologyRepair";

    PersistedSession persisted;
    persisted.schemaVersion = kSessionSchemaVersion;

    // Add workspace intents with invalid (zero) keys
    PersistedSession::PersistedWorkspaceIntent staleWs;
    staleWs.workspaceKey = 0; // Invalid
    staleWs.activity = OperationalActivity::Editing;
    staleWs.disposition = IntentDisposition::Persistent;
    persisted.workspaceIntents.push_back(staleWs);

    auto corruption = persistence_.diagnoseCorruption(persisted);

    bool hasStaleScope = false;
    for (auto scope : corruption.corruptedScopes) {
        if (scope == SessionCorruptionScope::StaleWorkspaceIds) {
            hasStaleScope = true;
            break;
        }
    }

    if (!hasStaleScope) {
        r.failureReason = "Stale workspace IDs not detected";
    } else {
        // Should still be restorable in topology-only mode
        bool canRestore = persistence_.canRestore(corruption, PartialRestoreMode::RestoreTopologyOnly);
        if (!canRestore) {
            r.failureReason = "Cannot restore with stale workspace IDs in topology-only mode";
        } else {
            r.passed = true;
        }
    }

    return r;
}

PersistenceWarfareValidator::TestResult PersistenceWarfareValidator::testWorkflowCorruptionRecovery() {
    TestResult r;
    r.testName = "WorkflowCorruptionRecovery";

    PersistedSession persisted;
    persisted.schemaVersion = kSessionSchemaVersion;

    // Add broken workflow edges (zero IDs)
    PersistedSession::PersistedWorkflowEdge brokenEdge;
    brokenEdge.fromSurface = 0;
    brokenEdge.toSurface = 0;
    brokenEdge.relationship = 0;
    persisted.workflowEdges.push_back(brokenEdge);

    auto corruption = persistence_.diagnoseCorruption(persisted);

    bool hasWorkflowCorruption = false;
    for (auto scope : corruption.corruptedScopes) {
        if (scope == SessionCorruptionScope::WorkflowCorruption) {
            hasWorkflowCorruption = true;
            break;
        }
    }

    if (!hasWorkflowCorruption) {
        r.failureReason = "Workflow corruption not detected";
    } else {
        // Should be restorable without workflows
        bool canRestore = persistence_.canRestore(corruption, PartialRestoreMode::RestoreWhatIsAvailable);
        if (!canRestore) {
            r.failureReason = "Cannot best-effort restore with workflow corruption";
        } else {
            r.passed = true;
        }
    }

    return r;
}

PersistenceWarfareValidator::TestResult PersistenceWarfareValidator::testPartialMinimalRestore() {
    TestResult r;
    r.testName = "PartialMinimalRestore";

    // Set up real state
    WorkspaceId wsA = workspaceCtrl_.activeWorkspace();
    WorkspaceId wsB = workspaceCtrl_.createWorkspace();

    intents_.setIntent(wsA, OperationalActivity::Editing,
                       IntentDisposition::ContinuityCritical);
    intents_.setIntent(wsB, OperationalActivity::Searching,
                       IntentDisposition::Transient);

    SessionState session;
    session.interruption.reason = InterruptionReason::CrashRecovery;

    WorkspaceSessionState wsStateA;
    wsStateA.workspaceKey = wsA.value;
    wsStateA.intent = intents_.getIntent(wsA);
    wsStateA.wasFocusedAtInterruption = true;

    WorkspaceSessionState wsStateB;
    wsStateB.workspaceKey = wsB.value;
    wsStateB.intent = intents_.getIntent(wsB);
    wsStateB.wasFocusedAtInterruption = false;

    session.workspaces.push_back(wsStateA);
    session.workspaces.push_back(wsStateB);

    // RestoreMinimal should suggest only ContinuityCritical
    auto policy = SessionState::suggestPolicyForInterruption(InterruptionReason::CrashRecovery);
    if (policy != SessionRestorePolicy::RestoreMinimal) {
        r.failureReason = "Crash recovery should suggest RestoreMinimal";
    } else {
        // In minimal mode, only ContinuityCritical workspaces should restore
        // wsA (ContinuityCritical) should restore, wsB (Transient) should not
        bool wsARestore = (wsStateA.intent.disposition == IntentDisposition::ContinuityCritical);
        bool wsBSkip = (wsStateB.intent.disposition == IntentDisposition::Transient);
        r.passed = wsARestore && wsBSkip;
    }

    intents_.removeWorkspace(wsB);
    workspaceCtrl_.destroyWorkspace(wsB);
    return r;
}

PersistenceWarfareValidator::TestResult PersistenceWarfareValidator::testFullRoundTripWithCorruption() {
    TestResult r;
    r.testName = "FullRoundTripWithCorruption";

    // Set up real state
    intents_.setIntent(workspaceCtrl_.activeWorkspace(),
                       OperationalActivity::Debugging,
                       IntentDisposition::InterruptSensitive);

    FocusNode node;
    node.id = 70001;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, workspaceCtrl_.activeWorkspace().value);

    budget_.setSurfaceAttention(node.id, AttentionLevel::Active);

    workflows_.addRelationship(node.id, 70002, WorkflowRelationship::CoEditing);

    // Capture
    SessionState session;
    session.interruption.reason = InterruptionReason::ForcedSuspend;
    PersistedSession persisted = persistence_.captureSession(session, intents_, workflows_, budget_);

    // Inject corruption: add broken edge
    PersistedSession::PersistedWorkflowEdge broken;
    broken.fromSurface = 0;
    broken.toSurface = 0;
    persisted.workflowEdges.push_back(broken);

    // Diagnose
    auto corruption = persistence_.diagnoseCorruption(persisted);
    if (corruption.isClean) {
        r.failureReason = "Injected corruption not detected";
        graph_.unregisterNode(node.id);
        workflows_.removeSurface(node.id);
        budget_.removeSurface(node.id);
        return r;
    }

    // Restore despite corruption
    bool canRestore = persistence_.canRestore(corruption, PartialRestoreMode::RestoreWhatIsAvailable);
    if (!canRestore) {
        r.failureReason = "Cannot restore despite best-effort mode";
    } else {
        r.passed = true;
    }

    // Cleanup
    graph_.unregisterNode(node.id);
    workflows_.removeSurface(node.id);
    budget_.removeSurface(node.id);
    return r;
}

} // namespace morphic
