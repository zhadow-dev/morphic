#include "validation_runtime.h"
#include "../interaction/focus_graph.h"
#include "workspace_runtime.h"
#include <windows.h>

namespace morphic {

ValidationSuiteResult ValidationRuntime::runAll() {
    ValidationSuiteResult result;

    if (!initialized_ || !graph_ || !wsRuntime_) {
        OutputDebugStringA("VALIDATION: Not initialized — cannot run\n");
        return result;
    }

    auto& ws = wsRuntime_->workspaceController();
    auto& intents = wsRuntime_->intentGraph();
    auto& budget = wsRuntime_->attentionBudget();
    auto& workflows = wsRuntime_->workflowGraph();

    // 1. Scaling Validator
    {
        ScalingValidator sv(*graph_, ws);
        auto r = sv.runFullValidation(8);
        ValidationSuiteResult::SuiteResult sr;
        sr.suiteName = "ScalingValidator";
        sr.total = r.totalTests;
        sr.passed = r.passed;
        sr.failed = r.failed;
        result.suites.push_back(sr);
    }

    // 2. Fault Domain Validator
    {
        FaultDomainValidator fv(*graph_, ws);
        auto r = fv.runFullValidation();
        ValidationSuiteResult::SuiteResult sr;
        sr.suiteName = "FaultDomainValidator";
        sr.total = r.totalTests;
        sr.passed = r.passed;
        sr.failed = r.failed;
        result.suites.push_back(sr);
    }

    // 3. Scheduler Validator
    {
        RuntimePressureEvaluator pressure;
        SchedulerValidator schv(*graph_, ws, pressure);
        auto r = schv.runFullValidation();
        ValidationSuiteResult::SuiteResult sr;
        sr.suiteName = "SchedulerValidator";
        sr.total = r.totalTests;
        sr.passed = r.passed;
        sr.failed = r.failed;
        result.suites.push_back(sr);
    }

    // 4. Runtime Drift Validator
    {
        RuntimeDriftValidator dv(*graph_, ws);
        auto r = dv.runDriftValidation(50);
        ValidationSuiteResult::SuiteResult sr;
        sr.suiteName = "RuntimeDriftValidator";
        sr.total = 1;
        sr.passed = r.convergent ? 1 : 0;
        sr.failed = r.convergent ? 0 : 1;
        result.suites.push_back(sr);
    }

    // 5. Persistence Warfare Validator
    {
        PersistenceWarfareValidator pv(*graph_, ws, intents, budget, workflows);
        auto r = pv.runFullValidation();
        ValidationSuiteResult::SuiteResult sr;
        sr.suiteName = "PersistenceWarfareValidator";
        sr.total = r.totalTests;
        sr.passed = r.passed;
        sr.failed = r.failed;
        result.suites.push_back(sr);
    }

    // 6. API Misuse Validator
    {
        APIMisuseValidator mv(*graph_, ws, intents, budget, workflows);
        auto r = mv.runFullValidation();
        ValidationSuiteResult::SuiteResult sr;
        sr.suiteName = "APIMisuseValidator";
        sr.total = r.totalTests;
        sr.passed = r.passed;
        sr.failed = r.failed;
        result.suites.push_back(sr);
    }

    // Totals
    for (const auto& s : result.suites) {
        result.totalTests += s.total;
        result.totalPassed += s.passed;
        result.totalFailed += s.failed;
    }

    OutputDebugStringA(("VALIDATION: " + std::to_string(result.totalPassed) +
        "/" + std::to_string(result.totalTests) + " tests passed across " +
        std::to_string(result.suites.size()) + " suites\n").c_str());

    return result;
}

} // namespace morphic
