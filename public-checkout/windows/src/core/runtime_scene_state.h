#pragma once

#include <unordered_map>
#include <memory>
#include "types.h"
#include "../interaction/focus_types.h"
#include "../experimental/attention_economics.h"
#include "../composition/workspace_controller.h"

namespace morphic {

// Represents the divergence severity levels
enum class DivergenceSeverity {
    Transient,    // Normal eventual consistency (< 3 ticks)
    Persistent,   // Realization taking longer (3-10 ticks)
    Critical,     // Realization repeatedly blocked/failed (> 10 ticks)
    Terminal      // Unrecoverable, quarantined surface
};

struct SurfaceSceneState {
    NodeId surfaceId = kInvalidNodeId;
    
    // --- Desired Semantic State (Expected Truth, writable ONLY by scheduler) ---
    bool desiredVisible = true;
    Transform desiredGeometry;
    SurfaceRole desiredRole = SurfaceRole::Workspace;
    ElevationLayer desiredElevation = ElevationLayer::Base;
    int desiredSublevel = 0;
    bool desiredActive = false;
    
    // Continuity & Orchestration Semantics (Phases 5-8 Integration)
    ContinuityState continuity = ContinuityState::Coherent;
    AttentionLevel attention = AttentionLevel::Background;
    WorkspaceId workspaceId = WorkspaceId::defaultId();
    SemanticVisibility semanticVisibility = SemanticVisibility::Full;
    RuntimePresence presence = RuntimePresence::ResidencyBudgeted;
    
    // --- Realized Physical State (What Win32/OS has actually realized) ---
    bool realizedVisible = false;
    Transform realizedGeometry;
    SurfaceRole realizedRole = SurfaceRole::Workspace;
    ElevationLayer realizedElevation = ElevationLayer::Base;
    int realizedSublevel = 0;
    bool realizedActive = false;
    
    // --- Divergence Status & Ticks ---
    bool hasGeometryDivergence = false;
    bool hasVisibilityDivergence = false;
    bool hasActivationDivergence = false;
    int divergenceTicks = 0;
    DivergenceSeverity severity = DivergenceSeverity::Transient;
};

// Protects who can write to the Desired State of the scene
class RuntimeSceneMutationAuthority {
public:
    explicit RuntimeSceneMutationAuthority(bool allowed) : allowed_(allowed) {}
    bool isAllowed() const { return allowed_; }
private:
    bool allowed_ = false;
};

class RuntimeSceneState {
public:
    RuntimeSceneState();
    ~RuntimeSceneState() = default;

    // --- Double-Buffering Swap ---
    void swapCommitBoundary();

    // --- Read-Only Query Access (Committed State) ---
    const std::unordered_map<NodeId, SurfaceSceneState>& committedStates() const { return committedStates_; }
    const SurfaceSceneState* getCommittedState(NodeId id) const;

    // --- Working State Writable APIs (Mutation Authority Protected) ---
    std::unordered_map<NodeId, SurfaceSceneState>& workingStates() { return workingStates_; }
    SurfaceSceneState* getWorkingState(NodeId id);
    SurfaceSceneState* getOrCreateWorkingState(NodeId id);

    // --- Writable Desired State Helpers ---
    void updateDesiredGeometry(const RuntimeSceneMutationAuthority& auth, NodeId id, const Transform& geom);
    void updateDesiredVisibility(const RuntimeSceneMutationAuthority& auth, NodeId id, bool visible);
    void updateDesiredRole(const RuntimeSceneMutationAuthority& auth, NodeId id, SurfaceRole role);
    void updateDesiredElevation(const RuntimeSceneMutationAuthority& auth, NodeId id, ElevationLayer layer, int sublevel);
    void updateDesiredActivation(const RuntimeSceneMutationAuthority& auth, NodeId id, bool active);
    void updateDesiredOrch(const RuntimeSceneMutationAuthority& auth, NodeId id, ContinuityState continuity, AttentionLevel attention, WorkspaceId workspace, SemanticVisibility visibility, RuntimePresence presence);

    // --- Writable Realized Observed Feedback APIs ---
    void updateRealizedGeometry(NodeId id, const Transform& geom);
    void updateRealizedVisibility(NodeId id, bool visible);
    void updateRealizedRole(NodeId id, SurfaceRole role);
    void updateRealizedElevation(NodeId id, ElevationLayer layer, int sublevel);
    void updateRealizedActivation(NodeId id, bool active);

    // --- Divergence Maintenance ---
    void checkDivergences();
    void clearState(NodeId id);

private:
    // Read-only state consumed by diagnostics, validators, and external APIs
    std::unordered_map<NodeId, SurfaceSceneState> committedStates_;
    
    // Scratchpad state used exclusively by scheduler during a commit tick
    std::unordered_map<NodeId, SurfaceSceneState> workingStates_;
};

} // namespace morphic
