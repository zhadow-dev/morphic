#pragma once

#include "../core/types.h"
#include "../core/runtime_pressure.h"
#include "../core/degraded_runtime_policy.h"
#include <string>
#include <vector>

namespace morphic {

class FocusGraph;
class WorkspaceController;

// Phase 6G: Operational Battlefield Validator.
//
// The true runtime war phase. Integration test of all Phase 6 subsystems.
// Proves the runtime remains coherent, bounded, recoverable, observable,
// and degradable under sustained operational pressure.

class OperationalBattlefieldValidator {
public:
    struct BattleResult {
        std::string scenarioName;
        bool survived = false;
        bool semanticIntegrity = true;
        bool operationalSurvivability = true;
        bool boundedFailure = true;
        bool schedulerIntegrity = true;
        std::string failureReason;
    };

    struct BattleReport {
        int totalScenarios = 0;
        int survived = 0;
        int failed = 0;
        std::vector<BattleResult> results;
        bool allSurvived() const { return failed == 0; }
    };

    OperationalBattlefieldValidator(FocusGraph& graph, WorkspaceController& workspaceCtrl,
                                     RuntimePressureEvaluator& pressure);

    BattleReport runFullBattle();

    BattleResult scenarioSchedulerFlood();
    BattleResult scenarioMutationDrift();
    BattleResult scenarioDivergenceStormUnderPressure();
    BattleResult scenarioRendererCrashCascade();
    BattleResult scenarioRecoveryOnlyDegradedMode();
    BattleResult scenarioCrossWorkspaceFaultIsolation();
    BattleResult scenarioTransactionStarvation();
    BattleResult scenarioMassiveTopologyChurn();
    BattleResult scenarioPressureEscalationLadder();
    BattleResult scenarioSustainedFailureSurvival();

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;
    RuntimePressureEvaluator& pressure_;
    DegradedRuntimePolicy degradedPolicy_;

    NodeId nextId_ = 50000;
    std::vector<NodeId> battleSurfaces_;

    NodeId createBattleSurface(uint64_t workspaceKey);
    void destroyBattleSurface(NodeId id);
    void cleanupBattle();
};

} // namespace morphic
