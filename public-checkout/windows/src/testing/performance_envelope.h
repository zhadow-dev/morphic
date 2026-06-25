#pragma once

#include "../core/types.h"
#include <string>
#include <vector>
#include <chrono>

namespace morphic {

class FocusGraph;
class WorkspaceController;
class WorkflowGraph;

// Phase 9E: Performance Envelope Characterization.
//
// Not optimization. Measurement.
// Defines the operational envelope: how many workspaces, surfaces,
// workflow edges, and topology mutations the runtime can sustain.

struct PerformanceMeasurement {
    std::string testName;
    int operationCount = 0;
    double totalMs = 0.0;
    double avgMicroseconds = 0.0;
    double peakMicroseconds = 0.0;
    bool withinBudget = false;
};

class PerformanceEnvelope {
public:
    struct EnvelopeReport {
        std::vector<PerformanceMeasurement> measurements;
        std::string summary;
    };

    PerformanceEnvelope(FocusGraph& graph, WorkspaceController& workspaceCtrl,
                         WorkflowGraph& workflows);

    EnvelopeReport characterize();

    PerformanceMeasurement measureWorkspaceCreation(int count);
    PerformanceMeasurement measureSurfaceRegistration(int count);
    PerformanceMeasurement measureFocusTransition(int count);
    PerformanceMeasurement measureCheckpointRestore(int count);
    PerformanceMeasurement measureWorkflowEdgeInsertion(int count);
    PerformanceMeasurement measureTopologyMutation(int count);

private:
    FocusGraph& graph_;
    WorkspaceController& workspaceCtrl_;
    WorkflowGraph& workflows_;
};

} // namespace morphic
