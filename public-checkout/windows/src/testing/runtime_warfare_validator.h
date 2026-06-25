#pragma once

#include "../core/types.h"
#include "../core/runtime_commit_scheduler.h"
#include <string>
#include <vector>

namespace morphic {

// Phase 9B: Runtime Warfare Validator.
//
// Validates the runtime's temporal survivability and resilience
// under extremely hostile runtime conditions (cross-thread warfare,
// HWND recycling desyncs, reentrancy cascades, time dilation delays,
// and partial commit failures).

class RuntimeWarfareValidator {
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

    explicit RuntimeWarfareValidator(RuntimeCommitScheduler& scheduler);
    ~RuntimeWarfareValidator() = default;

    ValidationReport runFullValidation();

    // 1. Cross-Thread Mutation Warfare
    TestResult testCrossThreadMutationWarfare();

    // 2. HWND Recycling Validation (lineage & epoch desync check)
    TestResult testHWNDRecyclingValidation();

    // 3. Re-entrancy Firewall & Cascade Collapse
    TestResult testReentrancyFirewallBound();

    // 4. Time Dilation & Partial Commit Failure Warfare
    TestResult testTimeDilationAndPartialFailure();

    // 5. Resource Exhaustion Warfare (USER/GDI leaks/stresses)
    TestResult testResourceExhaustionWarfare();

    // 6. Multi-Monitor Topology Warfare
    TestResult testMultiMonitorTopologyWarfare();

private:
    RuntimeCommitScheduler& scheduler_;
};

} // namespace morphic
