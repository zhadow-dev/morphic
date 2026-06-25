#include "runtime_scene_state.h"
#include <cassert>

namespace morphic {

RuntimeSceneState::RuntimeSceneState() = default;

void RuntimeSceneState::swapCommitBoundary() {
    committedStates_ = workingStates_;
}

const SurfaceSceneState* RuntimeSceneState::getCommittedState(NodeId id) const {
    auto it = committedStates_.find(id);
    if (it != committedStates_.end()) {
        return &it->second;
    }
    return nullptr;
}

SurfaceSceneState* RuntimeSceneState::getWorkingState(NodeId id) {
    auto it = workingStates_.find(id);
    if (it != workingStates_.end()) {
        return &it->second;
    }
    return nullptr;
}

SurfaceSceneState* RuntimeSceneState::getOrCreateWorkingState(NodeId id) {
    auto it = workingStates_.find(id);
    if (it != workingStates_.end()) {
        return &it->second;
    }
    
    auto committedIt = committedStates_.find(id);
    if (committedIt != committedStates_.end()) {
        workingStates_[id] = committedIt->second;
    } else {
        SurfaceSceneState newState;
        newState.surfaceId = id;
        workingStates_[id] = newState;
    }
    return &workingStates_[id];
}

void RuntimeSceneState::updateDesiredGeometry(const RuntimeSceneMutationAuthority& auth, NodeId id, const Transform& geom) {
    assert(auth.isAllowed() && "Write to desired state without scene mutation authority!");
    if (!auth.isAllowed()) return;
    getOrCreateWorkingState(id)->desiredGeometry = geom;
}

void RuntimeSceneState::updateDesiredVisibility(const RuntimeSceneMutationAuthority& auth, NodeId id, bool visible) {
    assert(auth.isAllowed() && "Write to desired state without scene mutation authority!");
    if (!auth.isAllowed()) return;
    getOrCreateWorkingState(id)->desiredVisible = visible;
}

void RuntimeSceneState::updateDesiredRole(const RuntimeSceneMutationAuthority& auth, NodeId id, SurfaceRole role) {
    assert(auth.isAllowed() && "Write to desired state without scene mutation authority!");
    if (!auth.isAllowed()) return;
    getOrCreateWorkingState(id)->desiredRole = role;
}

void RuntimeSceneState::updateDesiredElevation(const RuntimeSceneMutationAuthority& auth, NodeId id, ElevationLayer layer, int sublevel) {
    assert(auth.isAllowed() && "Write to desired state without scene mutation authority!");
    if (!auth.isAllowed()) return;
    auto* state = getOrCreateWorkingState(id);
    state->desiredElevation = layer;
    state->desiredSublevel = sublevel;
}

void RuntimeSceneState::updateDesiredActivation(const RuntimeSceneMutationAuthority& auth, NodeId id, bool active) {
    assert(auth.isAllowed() && "Write to desired state without scene mutation authority!");
    if (!auth.isAllowed()) return;
    getOrCreateWorkingState(id)->desiredActive = active;
}

void RuntimeSceneState::updateDesiredOrch(const RuntimeSceneMutationAuthority& auth, NodeId id, ContinuityState continuity, AttentionLevel attention, WorkspaceId workspace, SemanticVisibility visibility, RuntimePresence presence) {
    assert(auth.isAllowed() && "Write to desired state without scene mutation authority!");
    if (!auth.isAllowed()) return;
    auto* state = getOrCreateWorkingState(id);
    state->continuity = continuity;
    state->attention = attention;
    state->workspaceId = workspace;
    state->semanticVisibility = visibility;
    state->presence = presence;
}

void RuntimeSceneState::updateRealizedGeometry(NodeId id, const Transform& geom) {
    getOrCreateWorkingState(id)->realizedGeometry = geom;
}

void RuntimeSceneState::updateRealizedVisibility(NodeId id, bool visible) {
    getOrCreateWorkingState(id)->realizedVisible = visible;
}

void RuntimeSceneState::updateRealizedRole(NodeId id, SurfaceRole role) {
    getOrCreateWorkingState(id)->realizedRole = role;
}

void RuntimeSceneState::updateRealizedElevation(NodeId id, ElevationLayer layer, int sublevel) {
    auto* state = getOrCreateWorkingState(id);
    state->realizedElevation = layer;
    state->realizedSublevel = sublevel;
}

void RuntimeSceneState::updateRealizedActivation(NodeId id, bool active) {
    getOrCreateWorkingState(id)->realizedActive = active;
}

void RuntimeSceneState::checkDivergences() {
    for (auto& pair : workingStates_) {
        SurfaceSceneState& state = pair.second;
        
        if (state.presence == RuntimePresence::Hibernating) {
            state.hasGeometryDivergence = false;
            state.hasVisibilityDivergence = false;
            state.hasActivationDivergence = false;
            state.divergenceTicks = 0;
            state.severity = DivergenceSeverity::Transient;
            continue;
        }

        if (state.presence == RuntimePresence::Quarantined) {
            state.hasGeometryDivergence = false;
            state.hasVisibilityDivergence = false;
            state.hasActivationDivergence = false;
            state.divergenceTicks = 0;
            state.severity = DivergenceSeverity::Terminal;
            continue;
        }

        state.hasGeometryDivergence = (state.desiredGeometry != state.realizedGeometry);
        state.hasVisibilityDivergence = (state.desiredVisible != state.realizedVisible);
        state.hasActivationDivergence = (state.desiredActive != state.realizedActive);
        
        bool isDivergent = state.hasGeometryDivergence || state.hasVisibilityDivergence || state.hasActivationDivergence;
        
        if (isDivergent) {
            state.divergenceTicks++;
            if (state.divergenceTicks > 20) {
                state.severity = DivergenceSeverity::Terminal;
                state.presence = RuntimePresence::Quarantined;
            } else if (state.divergenceTicks > 10) {
                state.severity = DivergenceSeverity::Critical;
            } else if (state.divergenceTicks > 3) {
                state.severity = DivergenceSeverity::Persistent;
            } else {
                state.severity = DivergenceSeverity::Transient;
            }
        } else {
            state.divergenceTicks = 0;
            state.severity = DivergenceSeverity::Transient;
        }
    }
}

void RuntimeSceneState::clearState(NodeId id) {
    workingStates_.erase(id);
    committedStates_.erase(id);
}

} // namespace morphic
