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
class AdaptiveOrchestrator;

// Phase 7F: Semantic Session Battlefield Validator.
//
// Validates that human semantic layers remain ADVISORY and
// NEVER corrupt runtime truth under any conditions.

class SessionBattlefieldValidator {
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

    SessionBattlefieldValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl,
                                 WorkspaceIntentGraph& intents, AttentionBudget& budget,
                                 WorkflowGraph& workflows);

    ValidationReport runFullValidation();

    // Intent preservation across workspace switches
    TestResult testIntentPreservationAcrossSwitches();

    // Attention budget over-budget does NOT degrade runtime
    TestResult testAttentionBudgetDoesNotDegradeRuntime();

    // Workflow graph removal coherence
    TestResult testWorkflowGraphRemovalCoherence();

    // Orchestration hints do not mutate focus graph
    TestResult testOrchestrationDoesNotMutateFocusGraph();

    // Session restore policy correctness for each interruption reason
    TestResult testSessionRestorePolicySuggestions();

    // Semantic confidence does not affect runtime decisions
    TestResult testSemanticConfidenceIsolation();

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;
    WorkspaceIntentGraph& intents_;
    AttentionBudget& budget_;
    WorkflowGraph& workflows_;
};

} // namespace morphic
