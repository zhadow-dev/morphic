#pragma once

#include "../core/types.h"
#include "../rendering/renderer_health.h"
#include <string>
#include <vector>

namespace morphic {

class FocusGraph;
class WorkspaceController;

// Phase 6D: Fault Domain Isolation Validator.
//
// Proves that renderer collapse NEVER:
//   - poisons unrelated workspaces
//   - corrupts continuity graphs
//   - invalidates semantic truth
//   - stalls scheduler progress
//
// Uses existing RendererFaultDomain from renderer_health.h.

struct FaultContainmentResult {
    std::string testName;
    bool contained = false;

    int affectedDomains = 0;          // How many fault domains were affected
    int propagationRadius = 0;        // How far the fault propagated (0 = isolated)
    bool continuityRecoveryPossible = true;
    bool schedulerIntegrityPreserved = true;
    bool semanticTruthCorrupted = false;

    std::string failureReason;
};

class FaultDomainValidator {
public:
    struct ValidationReport {
        int totalTests = 0;
        int passed = 0;
        int failed = 0;
        std::vector<FaultContainmentResult> results;
        bool allPassed() const { return failed == 0; }
    };

    FaultDomainValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl);

    ValidationReport runFullValidation();

    // Individual fault injection scenarios
    FaultContainmentResult testRendererCrash();
    FaultContainmentResult testRendererFreeze();
    FaultContainmentResult testRendererDelayedRecovery();
    FaultContainmentResult testRendererSilentDisappearance();
    FaultContainmentResult testWorkspaceDestructionMidRecovery();
    FaultContainmentResult testDetachedChainCollapse();

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;
};

} // namespace morphic
