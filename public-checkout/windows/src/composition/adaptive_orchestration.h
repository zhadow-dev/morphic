#pragma once

#include "../core/types.h"
#include "workspace_intent.h"
#include "session_continuity.h"
#include "workflow_graph.h"
#include "../experimental/attention_economics.h"
#include <vector>
#include <string>

namespace morphic {

// Phase 7E: Human-Centric Adaptive Orchestration.
//
// ===================================================================
// HARD ARCHITECTURAL CONSTRAINTS — NEVER VIOLATE
// ===================================================================
//
// AdaptiveOrchestrator MAY:
//   - recommend restore ordering
//   - recommend continuity prioritization
//   - recommend workspace surfacing
//   - recommend degraded-mode preferences
//
// AdaptiveOrchestrator MUST NEVER:
//   - mutate topology
//   - commit focus
//   - trigger realization
//   - change continuity truth
//   - override governance
//   - modify pressure state
//   - evict surfaces
//   - degrade runtime state
//
// This boundary is EXISTENTIAL.
// Human semantics are INTERPRETIVE.
// Runtime kernel is AUTHORITATIVE.
// ===================================================================

// What kind of suggestion is the orchestrator making?
enum class OrchestrationHintKind {
    RestoreOrder,           // "Restore workspace A before workspace B"
    ContinuityPriority,    // "Prioritize continuity for these surfaces"
    WorkspaceSurfacing,     // "Surface this workspace for the user"
    DegradationPreference,  // "If degradation needed, prefer these targets"
    WorkflowGrouping,       // "These surfaces belong together"
    AttentionShift          // "User's attention may be shifting here"
};

inline const char* toString(OrchestrationHintKind kind) {
    switch (kind) {
        case OrchestrationHintKind::RestoreOrder:         return "RestoreOrder";
        case OrchestrationHintKind::ContinuityPriority:   return "ContinuityPriority";
        case OrchestrationHintKind::WorkspaceSurfacing:   return "WorkspaceSurfacing";
        case OrchestrationHintKind::DegradationPreference:return "DegradationPreference";
        case OrchestrationHintKind::WorkflowGrouping:     return "WorkflowGrouping";
        case OrchestrationHintKind::AttentionShift:       return "AttentionShift";
    }
    return "Unknown";
}

// A single orchestration suggestion.
// Hints are consumed by the runtime at its discretion.
// The runtime MAY ignore any hint without consequence.
struct OrchestrationHint {
    OrchestrationHintKind kind = OrchestrationHintKind::RestoreOrder;
    SemanticConfidence confidence = SemanticConfidence::Inferred;

    // Target(s) of the hint
    std::vector<NodeId> targetSurfaces;
    uint64_t targetWorkspace = 0;

    // Human-readable reason
    std::string reason;
};

// Phase 7E: Adaptive Orchestrator.
//
// Reads: intent graph + attention budget + workflow relationships + session context
// Emits: orchestration hints (SUGGESTIONS ONLY)
//
// This is the interpretive layer that bridges human semantics
// and runtime kernel. It never crosses the authority boundary.
class AdaptiveOrchestrator {
public:
    AdaptiveOrchestrator() = default;

    // Generate restoration ordering hints based on session context.
    std::vector<OrchestrationHint> suggestRestoreOrder(
        const SessionState& session,
        const WorkspaceIntentGraph& intents) const
    {
        std::vector<OrchestrationHint> hints;

        // ContinuityCritical workspaces should restore first
        for (const auto& ws : session.workspaces) {
            if (ws.intent.disposition == IntentDisposition::ContinuityCritical) {
                OrchestrationHint hint;
                hint.kind = OrchestrationHintKind::RestoreOrder;
                hint.confidence = ws.intent.confidence;
                hint.targetWorkspace = ws.workspaceKey;
                hint.reason = "ContinuityCritical disposition";
                hints.push_back(hint);
            }
        }

        // Workspace that was focused at interruption should restore early
        if (session.interruption.focusedSurfaceAtInterruption != kInvalidNodeId) {
            OrchestrationHint hint;
            hint.kind = OrchestrationHintKind::RestoreOrder;
            hint.confidence = session.interruption.confidence;
            hint.targetWorkspace = session.interruption.activeWorkspaceAtInterruption;
            hint.reason = "Was focused at interruption";
            hints.push_back(hint);
        }

        return hints;
    }

    // Generate workflow grouping hints.
    std::vector<OrchestrationHint> suggestWorkflowGrouping(
        const WorkflowGraph& workflows,
        NodeId surfaceId) const
    {
        std::vector<OrchestrationHint> hints;

        auto peers = workflows.workflowPeers(surfaceId);
        if (!peers.empty()) {
            OrchestrationHint hint;
            hint.kind = OrchestrationHintKind::WorkflowGrouping;
            hint.confidence = SemanticConfidence::Inferred;
            hint.targetSurfaces = peers;
            hint.targetSurfaces.push_back(surfaceId);
            hint.reason = "Surfaces in same workflow";
            hints.push_back(hint);
        }

        return hints;
    }

    // Generate degradation preference hints based on attention budget.
    std::vector<OrchestrationHint> suggestDegradationPreference(
        const AttentionBudget& budget) const
    {
        std::vector<OrchestrationHint> hints;

        if (budget.isOverBudget()) {
            auto order = budget.restorationPriorityOrder();
            // Suggest degrading lowest-priority surfaces first
            // (order is highest-priority first, so reverse for degradation)
            if (order.size() > 1) {
                OrchestrationHint hint;
                hint.kind = OrchestrationHintKind::DegradationPreference;
                hint.confidence = SemanticConfidence::Inferred;
                // Last in restoration priority = first degradation candidate
                hint.targetSurfaces.push_back(order.back());
                hint.reason = "Lowest attention priority, budget exceeded";
                hints.push_back(hint);
            }
        }

        return hints;
    }
};

} // namespace morphic
