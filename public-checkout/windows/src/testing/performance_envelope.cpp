#include "performance_envelope.h"
#include "../interaction/focus_graph.h"
#include "../composition/workspace_controller.h"
#include "../composition/workflow_graph.h"
#include <windows.h>
#include <algorithm>

namespace morphic {

PerformanceEnvelope::PerformanceEnvelope(FocusGraph& graph, WorkspaceController& workspaceCtrl,
                                          WorkflowGraph& workflows)
    : graph_(graph), workspaceCtrl_(workspaceCtrl), workflows_(workflows) {}

PerformanceEnvelope::EnvelopeReport PerformanceEnvelope::characterize() {
    EnvelopeReport report;

    report.measurements.push_back(measureWorkspaceCreation(100));
    report.measurements.push_back(measureSurfaceRegistration(1000));
    report.measurements.push_back(measureFocusTransition(5000));
    report.measurements.push_back(measureCheckpointRestore(1000));
    report.measurements.push_back(measureWorkflowEdgeInsertion(5000));
    report.measurements.push_back(measureTopologyMutation(200));

    std::string summary = "PERFORMANCE ENVELOPE:\n";
    for (const auto& m : report.measurements) {
        summary += "  " + m.testName + ": " +
            std::to_string(m.operationCount) + " ops in " +
            std::to_string(m.totalMs) + "ms (avg " +
            std::to_string(m.avgMicroseconds) + "us)\n";
    }
    report.summary = summary;

    OutputDebugStringA(summary.c_str());
    return report;
}

PerformanceMeasurement PerformanceEnvelope::measureWorkspaceCreation(int count) {
    PerformanceMeasurement m;
    m.testName = "WorkspaceCreation";
    m.operationCount = count;

    std::vector<WorkspaceId> workspaces;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < count; i++) {
        workspaces.push_back(workspaceCtrl_.createWorkspace());
    }

    auto end = std::chrono::high_resolution_clock::now();
    m.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    m.avgMicroseconds = (m.totalMs * 1000.0) / count;
    m.withinBudget = (m.avgMicroseconds < 100.0); // <100us per workspace

    for (auto ws : workspaces) {
        workspaceCtrl_.destroyWorkspace(ws);
    }

    return m;
}

PerformanceMeasurement PerformanceEnvelope::measureSurfaceRegistration(int count) {
    PerformanceMeasurement m;
    m.testName = "SurfaceRegistration";
    m.operationCount = count;

    uint64_t ws = workspaceCtrl_.activeWorkspace().value;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < count; i++) {
        FocusNode node;
        node.id = 90000 + i;
        node.domain = FocusDomain::Workspace;
        node.behavior = AttentionBehavior::Interactive;
        node.currentEligibility = FocusEligibility::Eligible;
        node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
        graph_.registerNode(node, ws);
    }

    auto end = std::chrono::high_resolution_clock::now();
    m.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    m.avgMicroseconds = (m.totalMs * 1000.0) / count;
    m.withinBudget = (m.avgMicroseconds < 50.0); // <50us per surface

    for (int i = 0; i < count; i++) {
        graph_.unregisterNode(90000 + i);
    }

    return m;
}

PerformanceMeasurement PerformanceEnvelope::measureFocusTransition(int count) {
    PerformanceMeasurement m;
    m.testName = "FocusTransition";
    m.operationCount = count;

    FocusNode node;
    node.id = 91000;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(node.id, 1);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < count; i++) {
        graph_.evaluateTransition(InteractionIntent::CycleForward, FocusInitiator::UserInput);
    }

    auto end = std::chrono::high_resolution_clock::now();
    m.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    m.avgMicroseconds = (m.totalMs * 1000.0) / count;
    m.withinBudget = (m.avgMicroseconds < 10.0); // <10us per transition

    graph_.unregisterNode(node.id);
    return m;
}

PerformanceMeasurement PerformanceEnvelope::measureCheckpointRestore(int count) {
    PerformanceMeasurement m;
    m.testName = "CheckpointRestore";
    m.operationCount = count;

    FocusNode node;
    node.id = 92000;
    node.domain = FocusDomain::Workspace;
    node.behavior = AttentionBehavior::Interactive;
    node.currentEligibility = FocusEligibility::Eligible;
    node.restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    graph_.registerNode(node, workspaceCtrl_.activeWorkspace().value);
    graph_.commitRealizedActivation(node.id, 1);

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < count; i++) {
        AttentionCheckpoint cp = graph_.createCheckpoint();
        graph_.evaluateCheckpointRestore(cp);
    }

    auto end = std::chrono::high_resolution_clock::now();
    m.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    m.avgMicroseconds = (m.totalMs * 1000.0) / count;
    m.withinBudget = (m.avgMicroseconds < 20.0); // <20us per cycle

    graph_.unregisterNode(node.id);
    return m;
}

PerformanceMeasurement PerformanceEnvelope::measureWorkflowEdgeInsertion(int count) {
    PerformanceMeasurement m;
    m.testName = "WorkflowEdgeInsertion";
    m.operationCount = count;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < count; i++) {
        workflows_.addRelationship(93000 + i, 93001 + i, WorkflowRelationship::SharedContext);
    }

    auto end = std::chrono::high_resolution_clock::now();
    m.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    m.avgMicroseconds = (m.totalMs * 1000.0) / count;
    m.withinBudget = (m.avgMicroseconds < 5.0); // <5us per edge

    // Cleanup
    for (int i = 0; i < count; i++) {
        workflows_.removeSurface(93000 + i);
    }

    return m;
}

PerformanceMeasurement PerformanceEnvelope::measureTopologyMutation(int count) {
    PerformanceMeasurement m;
    m.testName = "TopologyMutation";
    m.operationCount = count;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < count; i++) {
        WorkspaceId ws = workspaceCtrl_.createWorkspace();
        FocusNode node;
        node.id = 94000 + i;
        node.domain = FocusDomain::Workspace;
        node.behavior = AttentionBehavior::Interactive;
        node.currentEligibility = FocusEligibility::Eligible;
        node.restorePolicy = FocusRestorePolicy::WorkspaceDefault;
        graph_.registerNode(node, ws.value);
        graph_.unregisterNode(node.id);
        workspaceCtrl_.destroyWorkspace(ws);
    }

    auto end = std::chrono::high_resolution_clock::now();
    m.totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    m.avgMicroseconds = (m.totalMs * 1000.0) / count;
    m.withinBudget = (m.avgMicroseconds < 200.0); // <200us per full cycle

    return m;
}

} // namespace morphic
