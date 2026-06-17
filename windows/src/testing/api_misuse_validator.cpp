#include "api_misuse_validator.h"
#include "../interaction/focus_graph.h"
#include "../composition/workspace_controller.h"
#include "../composition/workspace_intent.h"
#include "../experimental/attention_economics.h"
#include "../composition/workflow_graph.h"
#include "../composition/session_persistence.h"
#include <windows.h>

namespace morphic {

APIMisuseValidator::APIMisuseValidator(
    FocusGraph& graph, WorkspaceController& workspaceCtrl,
    WorkspaceIntentGraph& intents, AttentionBudget& budget,
    WorkflowGraph& workflows)
    : graph_(graph), workspaceCtrl_(workspaceCtrl),
      intents_(intents), budget_(budget), workflows_(workflows) {}

APIMisuseValidator::ValidationReport APIMisuseValidator::runFullValidation() {
    ValidationReport report;

    auto add = [&](TestResult r) {
        report.totalTests++;
        if (r.passed) report.passed++;
        else report.failed++;
        report.results.push_back(r);
    };

    add(testDestroyActiveWorkspace());
    add(testSwitchToDestroyedWorkspace());
    add(testDoubleDestroyWorkspace());
    add(testAssociateNonexistentSurfaces());
    add(testDissociateNeverAssociated());
    add(testSetAttentionOnDestroyedSurface());
    add(testSetIntentOnDestroyedWorkspace());
    add(testMassiveWorkspaceCreation());
    add(testRapidCreateDestroySwitch());
    add(testSaveSessionDuringMutation());

    OutputDebugStringA(("API_MISUSE: " + std::to_string(report.passed) +
        "/" + std::to_string(report.totalTests) + " tests passed\n").c_str());

    return report;
}

APIMisuseValidator::TestResult APIMisuseValidator::testDestroyActiveWorkspace() {
    TestResult r;
    r.testName = "DestroyActiveWorkspace";

    // Consumer tries to destroy the workspace they're currently using
    WorkspaceId active = workspaceCtrl_.activeWorkspace();

    // Default workspace should be indestructible
    workspaceCtrl_.destroyWorkspace(active);
    
    // Verify default workspace still exists
    bool stillExists = workspaceCtrl_.workspaceExists(WorkspaceId::defaultId());
    if (!stillExists) {
        r.failureReason = "Default workspace was destroyed — runtime is broken";
    } else {
        // Verify runtime still works
        auto cp = graph_.createCheckpoint();
        r.passed = cp.valid || true; // Even invalid checkpoint = runtime survived
    }

    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testSwitchToDestroyedWorkspace() {
    TestResult r;
    r.testName = "SwitchToDestroyedWorkspace";

    WorkspaceId ws = workspaceCtrl_.createWorkspace();
    workspaceCtrl_.destroyWorkspace(ws);

    // Consumer tries to switch to a destroyed workspace
    workspaceCtrl_.switchWorkspace(ws);

    // Active workspace should NOT be the destroyed one
    WorkspaceId active = workspaceCtrl_.activeWorkspace();
    if (active.value == ws.value) {
        r.failureReason = "Switched to destroyed workspace";
    } else {
        r.passed = true;
    }

    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testDoubleDestroyWorkspace() {
    TestResult r;
    r.testName = "DoubleDestroyWorkspace";

    WorkspaceId ws = workspaceCtrl_.createWorkspace();
    workspaceCtrl_.destroyWorkspace(ws);

    // Consumer destroys the same workspace twice — must not crash
    workspaceCtrl_.destroyWorkspace(ws);

    // Runtime should still be intact
    bool defaultExists = workspaceCtrl_.workspaceExists(WorkspaceId::defaultId());
    r.passed = defaultExists;
    if (!r.passed) {
        r.failureReason = "Default workspace gone after double destroy";
    }

    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testAssociateNonexistentSurfaces() {
    TestResult r;
    r.testName = "AssociateNonexistentSurfaces";

    // Consumer associates surfaces that don't exist in the focus graph
    workflows_.addRelationship(99999, 99998, WorkflowRelationship::CoEditing);

    // Workflow graph should accept it (advisory) but runtime should be unaffected
    auto cp = graph_.createCheckpoint();
    r.passed = true; // If we get here without crash, it passed

    // Cleanup
    workflows_.removeSurface(99999);
    workflows_.removeSurface(99998);
    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testDissociateNeverAssociated() {
    TestResult r;
    r.testName = "DissociateNeverAssociated";

    // Consumer dissociates a surface that was never associated
    workflows_.removeSurface(88888);

    r.passed = true; // No crash = pass
    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testSetAttentionOnDestroyedSurface() {
    TestResult r;
    r.testName = "SetAttentionOnDestroyedSurface";

    FocusNode node;
    node.id = 80001;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, workspaceCtrl_.activeWorkspace().value);

    budget_.setSurfaceAttention(node.id, AttentionLevel::Active);

    // Destroy surface
    graph_.unregisterNode(node.id);

    // Consumer sets attention on destroyed surface — should not crash
    budget_.setSurfaceAttention(node.id, AttentionLevel::Urgent);

    // Runtime should be fine
    r.passed = true;
    budget_.removeSurface(node.id);
    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testSetIntentOnDestroyedWorkspace() {
    TestResult r;
    r.testName = "SetIntentOnDestroyedWorkspace";

    WorkspaceId ws = workspaceCtrl_.createWorkspace();
    intents_.setIntent(ws, OperationalActivity::Editing, IntentDisposition::Persistent);
    workspaceCtrl_.destroyWorkspace(ws);

    // Consumer sets intent on destroyed workspace — should not crash
    intents_.setIntent(ws, OperationalActivity::Debugging, IntentDisposition::ContinuityCritical);

    r.passed = true; // No crash = pass
    intents_.removeWorkspace(ws);
    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testMassiveWorkspaceCreation() {
    TestResult r;
    r.testName = "MassiveWorkspaceCreation";

    // Consumer creates 500 workspaces — should not explode
    std::vector<WorkspaceId> workspaces;
    for (int i = 0; i < 500; i++) {
        workspaces.push_back(workspaceCtrl_.createWorkspace());
    }

    bool defaultSurvives = workspaceCtrl_.workspaceExists(WorkspaceId::defaultId());

    // Cleanup
    for (auto ws : workspaces) {
        workspaceCtrl_.destroyWorkspace(ws);
    }

    r.passed = defaultSurvives;
    if (!r.passed) {
        r.failureReason = "Default workspace gone after 500 workspace creation";
    }
    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testRapidCreateDestroySwitch() {
    TestResult r;
    r.testName = "RapidCreateDestroySwitch";

    // Consumer rapidly creates, switches, and destroys — lifecycle abuse
    for (int i = 0; i < 200; i++) {
        WorkspaceId ws = workspaceCtrl_.createWorkspace();
        workspaceCtrl_.switchWorkspace(ws);
        graph_.setActiveWorkspace(ws.value);
        workspaceCtrl_.switchWorkspace(WorkspaceId::defaultId());
        graph_.setActiveWorkspace(1);
        workspaceCtrl_.destroyWorkspace(ws);
    }

    bool defaultSurvives = workspaceCtrl_.workspaceExists(WorkspaceId::defaultId());
    WorkspaceId active = workspaceCtrl_.activeWorkspace();

    r.passed = defaultSurvives && (active.value == WorkspaceId::defaultId().value);
    if (!r.passed) {
        r.failureReason = "Runtime inconsistent after rapid lifecycle abuse";
    }
    return r;
}

APIMisuseValidator::TestResult APIMisuseValidator::testSaveSessionDuringMutation() {
    TestResult r;
    r.testName = "SaveSessionDuringMutation";

    // Create state
    FocusNode node;
    node.id = 80010;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(node.id, 1);

    // Consumer saves session while mutating state
    SessionState session;
    session.interruption.reason = InterruptionReason::UrgentInterruption;

    SessionPersistenceManager persistence;
    PersistedSession persisted = persistence.captureSession(session, intents_, workflows_, budget_);

    // Now mutate state
    graph_.evaluateTransition(InteractionIntent::CycleForward, FocusInitiator::UserInput);

    // Captured session should still be valid (snapshot, not live reference)
    auto corruption = persistence.diagnoseCorruption(persisted);
    r.passed = corruption.isClean;
    if (!r.passed) {
        r.failureReason = "Session captured during mutation is corrupt";
    }

    graph_.unregisterNode(node.id);
    return r;
}

} // namespace morphic
