#include "diagnostics_channel.h"
#include "morphic_runtime_impl.h"
#include "../../include/morphic/morphic_api.h"

namespace morphic {

void DiagnosticsChannel::handleGetDiagnostics(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    auto health = runtime_.getHealth();

    flutter::EncodableMap map;
    map[flutter::EncodableValue("workspaceCount")] =
        flutter::EncodableValue(health.workspaceCount);
    map[flutter::EncodableValue("surfaceCount")] =
        flutter::EncodableValue(health.surfaceCount);
    map[flutter::EncodableValue("healthyRenderers")] =
        flutter::EncodableValue(health.healthyRenderers);
    map[flutter::EncodableValue("totalRenderers")] =
        flutter::EncodableValue(health.totalRenderers);
    map[flutter::EncodableValue("underPressure")] =
        flutter::EncodableValue(health.underPressure);
    map[flutter::EncodableValue("degradedMode")] =
        flutter::EncodableValue(health.degradedModeActive);
    map[flutter::EncodableValue("summary")] =
        flutter::EncodableValue(health.summary);

    result->Success(flutter::EncodableValue(map));
}

void DiagnosticsChannel::handleRunValidation(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    auto report = runtime_.runValidation();

    flutter::EncodableMap map;
    map[flutter::EncodableValue("totalTests")] =
        flutter::EncodableValue(report.totalTests);
    map[flutter::EncodableValue("totalPassed")] =
        flutter::EncodableValue(report.totalPassed);
    map[flutter::EncodableValue("totalFailed")] =
        flutter::EncodableValue(report.totalFailed);
    map[flutter::EncodableValue("allPassed")] =
        flutter::EncodableValue(report.allPassed());

    flutter::EncodableList suites;
    for (const auto& s : report.suites) {
        flutter::EncodableMap suite;
        suite[flutter::EncodableValue("name")] =
            flutter::EncodableValue(s.suiteName);
        suite[flutter::EncodableValue("total")] =
            flutter::EncodableValue(s.total);
        suite[flutter::EncodableValue("passed")] =
            flutter::EncodableValue(s.passed);
        suite[flutter::EncodableValue("failed")] =
            flutter::EncodableValue(s.failed);
        suites.push_back(flutter::EncodableValue(suite));
    }
    map[flutter::EncodableValue("suites")] =
        flutter::EncodableValue(suites);

    result->Success(flutter::EncodableValue(map));
}

void DiagnosticsChannel::handleGetBootstrapPhase(
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    auto phase = runtime_.currentPhase();
    result->Success(flutter::EncodableValue(std::string(toString(phase))));
}

} // namespace morphic
