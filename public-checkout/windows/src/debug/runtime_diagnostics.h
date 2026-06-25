#pragma once

#include "../core/types.h"
#include "../core/runtime_pressure.h"
#include "../core/degraded_runtime_policy.h"
#include "../interaction/focus_types.h"
#include "../composition/workspace_controller.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace morphic {

// Phase 6E: Runtime Introspection Layer.
//
// Pure operational observability. NOT UI. NOT visualization.
// JSON-safe diagnostic snapshot of entire runtime state.

struct FocusGraphDiagnostics {
    NodeId currentFocus = kInvalidNodeId;
    NodeId previousFocus = kInvalidNodeId;
    int totalNodes = 0;
    int eligibleNodes = 0;
    int suppressedNodes = 0;
    int modalStackDepth = 0;
    ContinuityState globalContinuity = ContinuityState::Coherent;

    // Per-workspace chain lengths
    std::unordered_map<uint64_t, int> workspaceChainLengths;
    int detachedChainLength = 0;
};

struct WorkspaceDiagnostics {
    int workspaceCount = 0;
    uint64_t activeWorkspaceKey = 0;
    std::unordered_map<uint64_t, WorkspaceAttentionState> attentionStates;
    std::unordered_map<uint64_t, int> surfaceCounts;
};

struct TransactionDiagnostics {
    int pendingTransactions = 0;
    int committedThisWindow = 0;
    int rolledBackThisWindow = 0;
    int divergencesThisWindow = 0;
};

struct RendererDiagnostics {
    int totalRenderers = 0;
    int healthyRenderers = 0;
    int degradedRenderers = 0;
    int crashedRenderers = 0;
    int recoveringRenderers = 0;
    int totalCrashCount = 0;
};

struct ContinuityDiagnostics {
    int coherentSurfaces = 0;
    int fracturedSurfaces = 0;
    int reconstructingSurfaces = 0;
    int divergedSurfaces = 0;
    int totalRepairs = 0;
};

// The master diagnostic snapshot.
// Aggregates all subsystem diagnostics into a single inspectable object.
struct RuntimeDiagnosticSnapshot {
    // Timestamp (monotonic ticks)
    uint64_t snapshotEpoch = 0;

    FocusGraphDiagnostics focus;
    WorkspaceDiagnostics workspaces;
    TransactionDiagnostics transactions;
    RendererDiagnostics renderers;
    ContinuityDiagnostics continuity;
    RuntimePressureSnapshot pressure;
    RuntimeDegradedMode degradedMode = RuntimeDegradedMode::None;

    // Serializable summary string (for debug output)
    std::string toSummaryString() const {
        std::string s;
        s += "RUNTIME DIAGNOSTIC SNAPSHOT [epoch=" + std::to_string(snapshotEpoch) + "]\n";
        s += "  Focus: current=" + std::to_string(focus.currentFocus) +
             " prev=" + std::to_string(focus.previousFocus) +
             " nodes=" + std::to_string(focus.totalNodes) +
             " eligible=" + std::to_string(focus.eligibleNodes) +
             " modals=" + std::to_string(focus.modalStackDepth) + "\n";
        s += "  Workspaces: count=" + std::to_string(workspaces.workspaceCount) +
             " active=" + std::to_string(workspaces.activeWorkspaceKey) + "\n";
        s += "  Transactions: pending=" + std::to_string(transactions.pendingTransactions) +
             " committed=" + std::to_string(transactions.committedThisWindow) +
             " rolledBack=" + std::to_string(transactions.rolledBackThisWindow) +
             " divergences=" + std::to_string(transactions.divergencesThisWindow) + "\n";
        s += "  Renderers: total=" + std::to_string(renderers.totalRenderers) +
             " healthy=" + std::to_string(renderers.healthyRenderers) +
             " crashed=" + std::to_string(renderers.crashedRenderers) +
             " recovering=" + std::to_string(renderers.recoveringRenderers) + "\n";
        s += "  Continuity: coherent=" + std::to_string(continuity.coherentSurfaces) +
             " fractured=" + std::to_string(continuity.fracturedSurfaces) +
             " diverged=" + std::to_string(continuity.divergedSurfaces) + "\n";
        return s;
    }
};

} // namespace morphic
