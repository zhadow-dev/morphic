#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "workspace_snapshot.h"

namespace morphic {

// Phase 4 — Workspace identity.
struct WorkspaceId {
    uint64_t value = 0;

    bool operator==(const WorkspaceId& o) const { return value == o.value; }
    bool operator!=(const WorkspaceId& o) const { return value != o.value; }
    bool operator<(const WorkspaceId& o) const { return value < o.value; }

    static WorkspaceId defaultId() { return {1}; }
    bool isDefault() const { return value == 1; }
};

// Phase 5A: Per-workspace attention semantics.
// NOT a global boolean. Each workspace has independent attention state.
enum class WorkspaceAttentionState {
    Foreground,           // Active, visible, owns attention
    BackgroundCoherent,   // Hidden but continuity intact
    Suspended,            // Continuity fractured, needs reconstruction
    DetachedActive,       // Detached surfaces still active
    Passive               // Overlay/utility workspace
};

// Phase 5B: Per-workspace metadata.
struct WorkspaceState {
    WorkspaceId id;
    WorkspaceAttentionState attention = WorkspaceAttentionState::BackgroundCoherent;
    std::unordered_set<NodeId> members;     // Surfaces belonging to this workspace
    WorkspaceSnapshot snapshot;             // Last saved snapshot
};

// Phase 5B — WorkspaceController.
//
// Owns workspace semantics:
//   - workspace creation/destruction
//   - workspace switching (with correct ordering: visibility LAST)
//   - surface membership tracking
//   - per-workspace attention state
//   - persistence snapshots
//
// THREAD: UI thread only.
class WorkspaceController {
public:
    WorkspaceController();

    // --- Workspace Lifecycle ---

    WorkspaceId createWorkspace();
    void destroyWorkspace(WorkspaceId id);

    // --- Active Workspace ---

    WorkspaceId activeWorkspace() const { return activeWorkspace_; }

    // Phase 5B: Correct workspace switch ordering:
    //   1. checkpoint current workspace
    //   2. evaluate target workspace continuity
    //   3. arbitration (can we switch?)
    //   4. topology stage (prepare target surfaces)
    //   5. semantic commit (update active workspace, focus chains)
    //   6. realization (Win32 activation of target surfaces)
    //   7. visibility transition (hide old, show new — LAST)
    //   8. continuity verification (assert coherence post-switch)
    //
    // Returns true if switch succeeded, false if blocked.
    bool switchWorkspace(WorkspaceId target);

    // --- Surface Membership ---

    void addSurfaceToWorkspace(NodeId surfaceId, WorkspaceId workspaceId);
    void removeSurfaceFromWorkspace(NodeId surfaceId, WorkspaceId workspaceId);
    std::vector<NodeId> surfacesInWorkspace(WorkspaceId id) const;

    // --- Attention ---

    WorkspaceAttentionState attentionState(WorkspaceId id) const;

    // --- Persistence ---

    WorkspaceSnapshot saveSnapshot(WorkspaceId id) const;
    void restoreSnapshot(const WorkspaceSnapshot& snapshot);

    // --- Queries ---

    size_t workspaceCount() const { return workspaces_.size(); }
    bool workspaceExists(WorkspaceId id) const;

private:
    WorkspaceId activeWorkspace_ = WorkspaceId::defaultId();
    uint64_t nextWorkspaceId_ = 2;  // 1 is the default workspace

    std::unordered_map<uint64_t, WorkspaceState> workspaces_;
};

}  // namespace morphic
