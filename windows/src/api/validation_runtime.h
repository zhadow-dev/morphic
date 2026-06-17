#pragma once

#include "../core/types.h"
#include "../testing/scaling_validator.h"
#include "../testing/fault_domain_validator.h"
#include "../testing/scheduler_validator.h"
#include "../testing/runtime_drift_validator.h"
#include "../testing/persistence_warfare_validator.h"
#include "../testing/api_misuse_validator.h"
#include "../testing/performance_envelope.h"
#include <string>
#include <vector>

namespace morphic {

class FocusGraph;
class WorkspaceRuntime;

// ValidationRuntime — Validator Orchestration.
//
// Validators NEVER live in the plugin layer.
// Plugin layer is transport only.
// ValidationRuntime owns all validator instantiation and execution.

struct ValidationSuiteResult {
    struct SuiteResult {
        std::string suiteName;
        int total = 0;
        int passed = 0;
        int failed = 0;
    };

    std::vector<SuiteResult> suites;
    int totalTests = 0;
    int totalPassed = 0;
    int totalFailed = 0;
    bool allPassed() const { return totalFailed == 0; }
};

class ValidationRuntime {
public:
    ValidationRuntime() = default;

    void initialize(FocusGraph& graph, WorkspaceRuntime& wsRuntime) {
        graph_ = &graph;
        wsRuntime_ = &wsRuntime;
        initialized_ = true;
    }

    bool isInitialized() const { return initialized_; }

    ValidationSuiteResult runAll();

private:
    bool initialized_ = false;
    FocusGraph* graph_ = nullptr;
    WorkspaceRuntime* wsRuntime_ = nullptr;
};

} // namespace morphic
