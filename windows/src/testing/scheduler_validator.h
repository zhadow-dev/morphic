#pragma once

#include "../core/types.h"
#include "../core/runtime_pressure.h"
#include <string>
#include <vector>

namespace morphic {

class FocusGraph;
class WorkspaceController;

// Phase 6B: Scheduler Survivability Validator.
//
// Validates the runtime survives:
//   - transaction floods
//   - delayed realization
//   - activation starvation
//   - topology commit floods
//   - rollback storms
//
// WITHOUT: deadlock, queue explosion, semantic corruption, infinite recursion.

class SchedulerValidator {
public:
    struct TestResult {
        std::string testName;
        bool passed = false;
        std::string failureReason;

        bool queueBounded = true;
        bool rollbackFinite = true;
        bool divergenceIsolated = true;
        bool noDeadlock = true;
        bool noSemanticCorruption = true;
    };

    struct ValidationReport {
        int totalTests = 0;
        int passed = 0;
        int failed = 0;
        std::vector<TestResult> results;
        bool allPassed() const { return failed == 0; }
    };

    SchedulerValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl,
                       RuntimePressureEvaluator& pressure);

    ValidationReport runFullValidation();

    TestResult testTransactionFlood();
    TestResult testDelayedRealization();
    TestResult testActivationStarvation();
    TestResult testTopologyCommitFlood();
    TestResult testRollbackStorm();

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;
    RuntimePressureEvaluator& pressure_;
};

} // namespace morphic
