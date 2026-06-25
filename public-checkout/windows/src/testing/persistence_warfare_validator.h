#pragma once

#include "../core/types.h"
#include "../composition/session_persistence.h"
#include <string>
#include <vector>

namespace morphic {

class FocusGraph;
class WorkspaceController;
class WorkspaceIntentGraph;
class AttentionBudget;
class WorkflowGraph;

// Phase 9C: Persistence & Recovery Warfare Validator.
//
// The true production battlefield. Validates the persistence layer
// survives real-world corruption, crashes, and partial failures.
//
// No new abstractions — only validation of existing systems.

class PersistenceWarfareValidator {
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

    PersistenceWarfareValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl,
                                 WorkspaceIntentGraph& intents, AttentionBudget& budget,
                                 WorkflowGraph& workflows);

    ValidationReport runFullValidation();

    // Crash mid-write: session capture interrupted, partial data
    TestResult testCrashMidWrite();

    // Stale schema: persisted session has wrong version
    TestResult testStaleSchemaRejection();

    // Orphan topology: workspace references that don't exist
    TestResult testOrphanTopologyRepair();

    // Workflow corruption: edges reference deleted surfaces
    TestResult testWorkflowCorruptionRecovery();

    // Partial restore: only ContinuityCritical workspaces restored
    TestResult testPartialMinimalRestore();

    // Full round-trip: capture → corrupt → diagnose → partial restore
    TestResult testFullRoundTripWithCorruption();

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;
    WorkspaceIntentGraph& intents_;
    AttentionBudget& budget_;
    WorkflowGraph& workflows_;
    SessionPersistenceManager persistence_;
};

} // namespace morphic
