#pragma once

#include "runtime_events.h"
#include "types.h"
#include "surface_role.h"
#include <vector>
#include <functional>
#include <string>

namespace morphic {

// Phase 4 — RuntimeTransaction.
//
// Atomic multi-mutation operations for the runtime.
// Without this, workspace switching, modal capture, and overview
// transitions produce visible intermediate states.
//
// DESIGN:
//   1. Begin transaction
//   2. Queue mutations (activate, raise, topology, role change, etc.)
//   3. Commit: apply all mutations, then flush transactional events
//   4. Rollback: discard all queued mutations and events
//
// This is NOT CompositionTransaction (which handles frame-level
// surface position/size batching via DeferWindowPos).
//
// RuntimeTransaction operates at the runtime-semantic level:
//   - activation changes
//   - focus transitions
//   - topology mutations
//   - role promotions/demotions
//   - workspace switches
//   - modal capture/release
//
// THREAD: UI thread only.
// NESTING: Transactions do NOT nest. Starting a transaction while one
//          is active is a programming error (asserted in debug).

// Individual operation within a transaction.
struct RuntimeOp {
    enum class Type {
        ActivateSurface,
        DeactivateSurface,
        RaiseSurfaceGroup,
        ChangeRole,
        ApplyTopology,
        PushModalLock,
        PopModalLock,
        SwitchWorkspace,
        // Future:
        // DetachSurface,
        // DockSurface,
        // EnterOverview,
        // ExitOverview,
    };

    Type type;
    NodeId surfaceId = 0;
    SurfaceRole role = SurfaceRole::Workspace;       // For ChangeRole
    SurfaceRole previousRole = SurfaceRole::Workspace;
    uint64_t workspaceId = 0;                        // For SwitchWorkspace
    std::string reason;
};

class RuntimeTransaction {
public:
    RuntimeTransaction() = default;

    // --- Queue operations ---

    void activateSurface(NodeId id, const std::string& reason = "") {
        RuntimeOp op;
        op.type = RuntimeOp::Type::ActivateSurface;
        op.surfaceId = id;
        op.reason = reason;
        ops_.push_back(op);
    }

    void deactivateSurface(NodeId id) {
        RuntimeOp op;
        op.type = RuntimeOp::Type::DeactivateSurface;
        op.surfaceId = id;
        ops_.push_back(op);
    }

    void raiseSurfaceGroup(NodeId id) {
        RuntimeOp op;
        op.type = RuntimeOp::Type::RaiseSurfaceGroup;
        op.surfaceId = id;
        ops_.push_back(op);
    }

    void changeRole(NodeId id, SurfaceRole from, SurfaceRole to) {
        RuntimeOp op;
        op.type = RuntimeOp::Type::ChangeRole;
        op.surfaceId = id;
        op.previousRole = from;
        op.role = to;
        ops_.push_back(op);
    }

    void applyTopology(NodeId id) {
        RuntimeOp op;
        op.type = RuntimeOp::Type::ApplyTopology;
        op.surfaceId = id;
        ops_.push_back(op);
    }

    void pushModalLock(NodeId id) {
        RuntimeOp op;
        op.type = RuntimeOp::Type::PushModalLock;
        op.surfaceId = id;
        ops_.push_back(op);
    }

    void popModalLock() {
        RuntimeOp op;
        op.type = RuntimeOp::Type::PopModalLock;
        ops_.push_back(op);
    }

    void switchWorkspace(uint64_t targetWorkspaceId) {
        RuntimeOp op;
        op.type = RuntimeOp::Type::SwitchWorkspace;
        op.workspaceId = targetWorkspaceId;
        ops_.push_back(op);
    }

    // --- Query ---

    const std::vector<RuntimeOp>& operations() const { return ops_; }
    size_t operationCount() const { return ops_.size(); }
    bool isEmpty() const { return ops_.empty(); }

    // --- Lifecycle ---

    void clear() { ops_.clear(); }

private:
    std::vector<RuntimeOp> ops_;
};

}  // namespace morphic
