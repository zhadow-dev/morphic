#include "workspace_controller.h"
#include <windows.h>
#include <string>

namespace morphic {

WorkspaceController::WorkspaceController() {
    // Create the default workspace
    WorkspaceState defaultWs;
    defaultWs.id = WorkspaceId::defaultId();
    defaultWs.attention = WorkspaceAttentionState::Foreground;
    workspaces_[defaultWs.id.value] = defaultWs;
}

WorkspaceId WorkspaceController::createWorkspace() {
    WorkspaceId id;
    id.value = nextWorkspaceId_++;

    WorkspaceState ws;
    ws.id = id;
    ws.attention = WorkspaceAttentionState::BackgroundCoherent;
    workspaces_[id.value] = ws;

    OutputDebugStringA(("WORKSPACE: Created workspace #" +
        std::to_string(id.value) + "\n").c_str());

    return id;
}

void WorkspaceController::destroyWorkspace(WorkspaceId id) {
    if (id.isDefault()) {
        OutputDebugStringA("WORKSPACE: Cannot destroy default workspace.\n");
        return;
    }
    if (activeWorkspace_ == id) {
        OutputDebugStringA("WORKSPACE: Cannot destroy active workspace. Switch first.\n");
        return;
    }
    workspaces_.erase(id.value);
    OutputDebugStringA(("WORKSPACE: Destroyed workspace #" +
        std::to_string(id.value) + "\n").c_str());
}

bool WorkspaceController::switchWorkspace(WorkspaceId target) {
    if (target == activeWorkspace_) return true;

    auto targetIt = workspaces_.find(target.value);
    if (targetIt == workspaces_.end()) {
        OutputDebugStringA("WORKSPACE: Switch target does not exist.\n");
        return false;
    }

    auto currentIt = workspaces_.find(activeWorkspace_.value);

    // Phase 5B: Correct ordering — semantic commit before visibility.
    //
    // Step 1: Checkpoint current workspace (caller responsibility via FocusGraph)
    // Step 2: Evaluate target continuity (is it Suspended? Fractured?)
    if (targetIt->second.attention == WorkspaceAttentionState::Suspended) {
        OutputDebugStringA("WORKSPACE: Target workspace is Suspended. Marking Reconstructing.\n");
        targetIt->second.attention = WorkspaceAttentionState::BackgroundCoherent;
        // Continuity reconstruction would happen here via FocusGraph
    }

    // Step 3-4: Topology stage — no-op for now (surfaces already exist)

    // Step 5: Semantic commit — update active workspace
    if (currentIt != workspaces_.end()) {
        currentIt->second.attention = WorkspaceAttentionState::BackgroundCoherent;
    }
    activeWorkspace_ = target;
    targetIt->second.attention = WorkspaceAttentionState::Foreground;

    // Step 6: Realization — caller triggers via FocusGraph::setActiveWorkspace
    // Step 7: Visibility transition — caller hides/shows surfaces (LAST)
    // Step 8: Continuity verification — caller asserts coherence

    OutputDebugStringA(("WORKSPACE: Switched to workspace #" +
        std::to_string(target.value) + "\n").c_str());

    return true;
}

void WorkspaceController::addSurfaceToWorkspace(NodeId surfaceId, WorkspaceId workspaceId) {
    auto it = workspaces_.find(workspaceId.value);
    if (it != workspaces_.end()) {
        it->second.members.insert(surfaceId);
    }
}

void WorkspaceController::removeSurfaceFromWorkspace(NodeId surfaceId, WorkspaceId workspaceId) {
    auto it = workspaces_.find(workspaceId.value);
    if (it != workspaces_.end()) {
        it->second.members.erase(surfaceId);
    }
}

std::vector<NodeId> WorkspaceController::surfacesInWorkspace(WorkspaceId id) const {
    auto it = workspaces_.find(id.value);
    if (it != workspaces_.end()) {
        return std::vector<NodeId>(it->second.members.begin(), it->second.members.end());
    }
    return {};
}

WorkspaceAttentionState WorkspaceController::attentionState(WorkspaceId id) const {
    auto it = workspaces_.find(id.value);
    if (it != workspaces_.end()) {
        return it->second.attention;
    }
    return WorkspaceAttentionState::BackgroundCoherent;
}

WorkspaceSnapshot WorkspaceController::saveSnapshot(WorkspaceId id) const {
    WorkspaceSnapshot snap;
    // In a full implementation, iterate workspace members and build ordering intents.
    return snap;
}

void WorkspaceController::restoreSnapshot(const WorkspaceSnapshot& snapshot) {
    // In a full implementation, validate ordering intents against current topology
    // and re-realize z-order to match semantic intent.
}

bool WorkspaceController::workspaceExists(WorkspaceId id) const {
    return workspaces_.find(id.value) != workspaces_.end();
}

} // namespace morphic
