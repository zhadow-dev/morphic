#pragma once

#include "../core/types.h"
#include "../composition/workspace_controller.h"
#include "../composition/workspace_intent.h"
#include "../composition/workflow_graph.h"
#include "../composition/session_continuity.h"
#include "../composition/session_persistence.h"
#include "../composition/adaptive_orchestration.h"
#include "../experimental/attention_economics.h"
#include "../interaction/focus_graph.h"
#include <string>

namespace morphic {

// WorkspaceRuntime — Semantic Orchestration Layer.
//
// ARCHITECTURAL INVARIANT:
// WorkspaceRuntime is SEPARATE from Compositor.
// Compositor owns kernel coordination primitives.
// WorkspaceRuntime owns human semantic orchestration.
//
// WorkspaceRuntime reads FROM the kernel (FocusGraph, surface state)
// but NEVER mutates kernel authority directly.
// It routes requests THROUGH the kernel's public coordination methods.
//
// Compositor does NOT own:
//   - WorkspaceController
//   - WorkspaceIntentGraph
//   - WorkflowGraph
//   - AttentionBudget
//   - SessionPersistenceManager
//   - AdaptiveOrchestrator
//
// Those live HERE.

class WorkspaceRuntime {
public:
    WorkspaceRuntime() = default;

    // Initialize with kernel reference (read-only for semantic queries)
    void initialize(FocusGraph& focusGraph) {
        focusGraph_ = &focusGraph;
        initialized_ = true;
    }

    bool isInitialized() const { return initialized_; }

    // --- Workspace Lifecycle (delegates to WorkspaceController) ---
    WorkspaceId createWorkspace() {
        return workspaceCtrl_.createWorkspace();
    }

    void destroyWorkspace(WorkspaceId id) {
        intents_.removeWorkspace(id);
        workspaceCtrl_.destroyWorkspace(id);
    }

    void switchWorkspace(WorkspaceId target) {
        if (!focusGraph_) return;

        // 1. Checkpoint current workspace semantic state
        WorkspaceId current = workspaceCtrl_.activeWorkspace();
        if (current.value == target.value) return;

        // 2. Commit semantic transition
        workspaceCtrl_.switchWorkspace(target);

        // 3. Update FocusGraph active workspace
        focusGraph_->setActiveWorkspace(target.value);
    }

    WorkspaceId activeWorkspace() const {
        return workspaceCtrl_.activeWorkspace();
    }

    // --- Intent (advisory) ---
    void setWorkspaceIntent(WorkspaceId id, OperationalActivity activity,
                             IntentDisposition disposition,
                             const std::string& label = "") {
        intents_.setIntent(id, activity, disposition,
                           SemanticConfidence::Explicit, label);
    }

    WorkspaceIntentState getWorkspaceIntent(WorkspaceId id) const {
        return intents_.getIntent(id);
    }

    // --- Attention (advisory) ---
    void setSurfaceAttention(NodeId surfaceId, AttentionLevel level) {
        budget_.setSurfaceAttention(surfaceId, level);
    }

    // --- Workflow Associations (advisory, NOT dependencies) ---
    void associateSurfaces(NodeId a, NodeId b,
                            WorkflowRelationship relationship) {
        workflows_.addRelationship(a, b, relationship);
    }

    void dissociateSurface(NodeId surfaceId) {
        workflows_.removeSurface(surfaceId);
        budget_.removeSurface(surfaceId);
    }

    // --- Session ---
    void saveSession(InterruptionReason reason) {
        SessionState session;
        session.interruption.reason = reason;
        session.interruption.activeWorkspaceAtInterruption =
            workspaceCtrl_.activeWorkspace().value;
        session.restorePolicy =
            SessionState::suggestPolicyForInterruption(reason);

        lastPersistedSession_ =
            persistence_.captureSession(session, intents_, workflows_, budget_);
        sessionSaved_ = true;
    }

    bool restoreSession() {
        if (!sessionSaved_) return false;

        auto corruption = persistence_.diagnoseCorruption(lastPersistedSession_);
        return persistence_.canRestore(corruption,
                                        PartialRestoreMode::RestoreWhatIsAvailable);
    }

    // --- Orchestration Hints ---
    std::vector<OrchestrationHint> getOrchestrationHints() const {
        SessionState session;
        session.interruption.activeWorkspaceAtInterruption =
            workspaceCtrl_.activeWorkspace().value;
        return orchestrator_.suggestRestoreOrder(session, intents_);
    }

    // --- Accessors (for diagnostics / validation) ---
    WorkspaceController& workspaceController() { return workspaceCtrl_; }
    WorkspaceIntentGraph& intentGraph() { return intents_; }
    AttentionBudget& attentionBudget() { return budget_; }
    WorkflowGraph& workflowGraph() { return workflows_; }
    SessionPersistenceManager& persistence() { return persistence_; }

    // --- Telemetry & Privacy Boundaries ---
    void setOptInProductivity(bool enabled) {
        optInProductivity_ = enabled;
    }

    bool isOptInProductivity() const {
        return optInProductivity_;
    }

    int workspaceCount() const { return static_cast<int>(workspaceCtrl_.workspaceCount()); }
    int workflowEdgeCount() const { return static_cast<int>(workflows_.edgeCount()); }

private:
    bool initialized_ = false;
    bool sessionSaved_ = false;
    bool optInProductivity_ = false;
    FocusGraph* focusGraph_ = nullptr;

    WorkspaceController workspaceCtrl_;
    WorkspaceIntentGraph intents_;
    AttentionBudget budget_;
    WorkflowGraph workflows_;
    SessionPersistenceManager persistence_;
    AdaptiveOrchestrator orchestrator_;
    PersistedSession lastPersistedSession_;
};

} // namespace morphic
