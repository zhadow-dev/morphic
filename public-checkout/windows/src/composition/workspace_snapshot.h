#ifndef MORPHIC_SRC_COMPOSITION_WORKSPACE_SNAPSHOT_H_
#define MORPHIC_SRC_COMPOSITION_WORKSPACE_SNAPSHOT_H_

#include "../core/types.h"
#include "../interaction/focus_types.h"
#include <vector>

namespace morphic {

// Phase 5A: Workspace Persistence Model (corrected)

// Represents the semantic intent for a surface's position in the composition,
// decoupled from Win32 HWND z-ordering and runtime NodeId.
struct SurfaceOrderingIntent {
    uint64_t persistentId;    // Stable identity (NOT runtime NodeId)
    SurfaceRole role;
    AttentionBehavior behavior;
    bool prefersForeground;
};

// Phase 5A: Dependency topology for deterministic restore ordering.
// Without this, multi-workspace restore becomes nondeterministic.
struct SurfaceDependencyIntent {
    uint64_t persistentId;
    uint64_t requiredBeforeRestore = 0;  // Must exist before this surface restores
    uint64_t optionalDependency = 0;     // Preferred but not required
    bool suppressesWhenHidden = false;   // Hide this if dependency is hidden
};

// A persistable reconstruction artifact representing the entire workspace state.
// Contains semantic ordering intent and dependency topology.
// Does NOT contain live AttentionCheckpoints (those are runtime-only).
struct WorkspaceSnapshot {
    std::vector<SurfaceOrderingIntent> orderingIntents;
    std::vector<SurfaceDependencyIntent> dependencies;

    // Semantic focus intent — validated during reconstruction, not blindly applied.
    uint64_t expectedFocusPersistentId = 0;
};

} // namespace morphic

#endif // MORPHIC_SRC_COMPOSITION_WORKSPACE_SNAPSHOT_H_
