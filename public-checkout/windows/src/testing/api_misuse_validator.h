#pragma once

#include "../core/types.h"
#include <string>
#include <vector>

namespace morphic {

class FocusGraph;
class WorkspaceController;
class WorkspaceIntentGraph;
class AttentionBudget;
class WorkflowGraph;

// Phase 9D: API Misuse Resistance Validator.
//
// Proves the runtime remains coherent under consumer stupidity.
// No new abstractions — only hostile testing of existing systems.

class APIMisuseValidator {
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

    APIMisuseValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl,
                        WorkspaceIntentGraph& intents, AttentionBudget& budget,
                        WorkflowGraph& workflows);

    ValidationReport runFullValidation();

    TestResult testDestroyActiveWorkspace();
    TestResult testSwitchToDestroyedWorkspace();
    TestResult testDoubleDestroyWorkspace();
    TestResult testAssociateNonexistentSurfaces();
    TestResult testDissociateNeverAssociated();
    TestResult testSetAttentionOnDestroyedSurface();
    TestResult testSetIntentOnDestroyedWorkspace();
    TestResult testMassiveWorkspaceCreation();
    TestResult testRapidCreateDestroySwitch();
    TestResult testSaveSessionDuringMutation();

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;
    WorkspaceIntentGraph& intents_;
    AttentionBudget& budget_;
    WorkflowGraph& workflows_;
};

} // namespace morphic
