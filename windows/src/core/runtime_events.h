#pragma once

#include "../core/types.h"
#include "../core/surface_role.h"
#include "../rendering/renderer_health.h"
#include <variant>
#include <vector>
#include <functional>
#include <unordered_map>
#include <string>

namespace morphic {

// Phase 4 — Runtime Event System.
//
// Cross-subsystem communication via typed events.
// Prevents direct coupling between subsystems.
//
// Example: ActivationManager emits SurfaceActivated,
//          FocusGraph consumes it. Neither knows the other exists.
//
// DESIGN DECISION: Typed payloads via std::variant.
// NOT void*, NOT std::any, NOT JSON, NOT optional soup.
// Payloads are compile-time verified.
//
// THREAD: UI thread only. Events are synchronous dispatch.
//         If async dispatch is needed later, add a queue.

// --- Event types ---
enum class RuntimeEventType {
    // Lifecycle
    SurfaceCreated,
    SurfaceDestroyed,
    SurfaceRoleChanged,

    // Activation
    SurfaceActivated,
    SurfaceDeactivated,
    MainWindowActivated,
    ExternalFocusSteal,

    // Renderer
    RendererAttached,
    RendererDetached,
    RendererHealthChanged,

    // Orchestration
    OrchestrationTransition,
    ResidencyChanged,

    // Topology
    TopologyMutated,
    ZOrderChanged,

    // Workspace
    WorkspaceSwitched,

    // Runtime
    PhaseChanged,
};

// --- Typed payloads ---

struct SurfaceEventPayload {
    NodeId surfaceId = 0;
    SurfaceRole role = SurfaceRole::Workspace;
    SurfaceRole previousRole = SurfaceRole::Workspace;  // For role changes
};

struct ActivationEventPayload {
    NodeId surfaceId = 0;
    NodeId previousSurfaceId = 0;
    std::string reason;
};

struct RendererEventPayload {
    NodeId surfaceId = 0;
    RenderId rendererId = kInvalidRenderId;
    RendererHealth health = RendererHealth::Healthy;
    RendererHealth previousHealth = RendererHealth::Healthy;
};

struct TopologyEventPayload {
    NodeId surfaceId = 0;
    // What changed: style, owner, parent, z-order, etc.
    // Intentionally opaque for now — becomes structured in Phase 4B.
    std::string mutation;
};

struct WorkspaceEventPayload {
    uint64_t workspaceId = 0;
    uint64_t previousWorkspaceId = 0;
};

struct PhaseEventPayload {
    int fromPhase = 0;  // Cast from RuntimePhase
    int toPhase = 0;
};

struct OrchestrationEventPayload {
    NodeId surfaceId = 0;
    int fromState = 0;  // Cast from ActivityState
    int toState = 0;
    std::string reason;
};

// --- Runtime Event ---

using RuntimeEventPayload = std::variant<
    SurfaceEventPayload,
    ActivationEventPayload,
    RendererEventPayload,
    TopologyEventPayload,
    WorkspaceEventPayload,
    PhaseEventPayload,
    OrchestrationEventPayload
>;

struct RuntimeEvent {
    RuntimeEventType type;
    RuntimeEventPayload payload;

    // Convenience constructors
    static RuntimeEvent surfaceCreated(NodeId id, SurfaceRole role) {
        return { RuntimeEventType::SurfaceCreated,
                 SurfaceEventPayload{id, role, role} };
    }

    static RuntimeEvent surfaceDestroyed(NodeId id) {
        return { RuntimeEventType::SurfaceDestroyed,
                 SurfaceEventPayload{id} };
    }

    static RuntimeEvent surfaceRoleChanged(NodeId id, SurfaceRole from, SurfaceRole to) {
        return { RuntimeEventType::SurfaceRoleChanged,
                 SurfaceEventPayload{id, to, from} };
    }

    static RuntimeEvent surfaceActivated(NodeId id, NodeId prevId, const std::string& reason = "") {
        return { RuntimeEventType::SurfaceActivated,
                 ActivationEventPayload{id, prevId, reason} };
    }

    static RuntimeEvent mainWindowActivated() {
        return { RuntimeEventType::MainWindowActivated,
                 ActivationEventPayload{} };
    }

    static RuntimeEvent rendererAttached(NodeId surfaceId, RenderId rendererId) {
        return { RuntimeEventType::RendererAttached,
                 RendererEventPayload{surfaceId, rendererId} };
    }

    static RuntimeEvent rendererDetached(NodeId surfaceId, RenderId rendererId) {
        return { RuntimeEventType::RendererDetached,
                 RendererEventPayload{surfaceId, rendererId} };
    }

    static RuntimeEvent rendererHealthChanged(NodeId surfaceId, RenderId rendererId,
                                               RendererHealth from, RendererHealth to) {
        return { RuntimeEventType::RendererHealthChanged,
                 RendererEventPayload{surfaceId, rendererId, to, from} };
    }

    static RuntimeEvent phaseChanged(int from, int to) {
        return { RuntimeEventType::PhaseChanged,
                 PhaseEventPayload{from, to} };
    }

    static RuntimeEvent topologyMutated(NodeId surfaceId, const std::string& mutation) {
        return { RuntimeEventType::TopologyMutated,
                 TopologyEventPayload{surfaceId, mutation} };
    }
};

// --- Event Dispatch Semantics ---
//
// NOT all events should dispatch immediately.
// Without dispatch phases, you get:
//   - recursive activation (activate → raise → activate)
//   - topology mutation loops (role change → topology → role check)
//   - re-entrant governance (transition → eval → transition)
//   - z-order recursion (raise → activate → raise)
//
enum class RuntimeEventDispatch {
    // Dispatch to listeners immediately during emit().
    // Use for: phase changes, health monitoring, logging.
    // DANGER: listeners may trigger further emissions.
    Immediate,

    // Queue for dispatch at end of current operation.
    // Use for: activation, topology, z-order changes.
    // Prevents recursive mutation during a single logical operation.
    Deferred,

    // Queue for dispatch when RuntimeTransaction commits.
    // Use for: workspace switching, modal capture, multi-surface operations.
    // Groups multiple mutations into one atomic notification batch.
    Transactional,
};

// --- Event Listener ---

class RuntimeEventListener {
public:
    virtual ~RuntimeEventListener() = default;
    virtual void onRuntimeEvent(const RuntimeEvent& event) = 0;
};

// --- Event Bus ---
//
// Owned by RuntimeKernel. Distributes events to subscribers.
// Supports three dispatch modes to prevent runtime recursion.
//
// Subsystems subscribe to event types they care about.
// No subsystem should subscribe to everything — that defeats the purpose.
//
// THREAD: UI thread only.
class RuntimeEventBus {
public:
    RuntimeEventBus() = default;

    void subscribe(RuntimeEventType type, RuntimeEventListener* listener) {
        listeners_[static_cast<int>(type)].push_back(listener);
    }

    void unsubscribe(RuntimeEventListener* listener) {
        for (auto& [type, vec] : listeners_) {
            vec.erase(
                std::remove(vec.begin(), vec.end(), listener),
                vec.end());
        }
    }

    // Emit with explicit dispatch semantics.
    void emit(const RuntimeEvent& event,
              RuntimeEventDispatch dispatch = RuntimeEventDispatch::Immediate) {
        switch (dispatch) {
            case RuntimeEventDispatch::Immediate:
                dispatchNow(event);
                break;
            case RuntimeEventDispatch::Deferred:
                deferredQueue_.push_back(event);
                break;
            case RuntimeEventDispatch::Transactional:
                transactionalQueue_.push_back(event);
                break;
        }
    }

    // Flush all deferred events. Call at end of a logical operation.
    // (e.g., after ActivationManager finishes DeferWindowPos)
    void flushDeferred() {
        // Process queue — new deferred events during flush go to back
        while (!deferredQueue_.empty()) {
            auto batch = std::move(deferredQueue_);
            deferredQueue_.clear();
            for (const auto& event : batch) {
                dispatchNow(event);
            }
        }
    }

    // Flush all transactional events. Called by RuntimeTransaction::commit().
    void flushTransactional() {
        auto batch = std::move(transactionalQueue_);
        transactionalQueue_.clear();
        for (const auto& event : batch) {
            dispatchNow(event);
        }
    }

    // Discard transactional events. Called by RuntimeTransaction::rollback().
    void discardTransactional() {
        transactionalQueue_.clear();
    }

    bool hasPendingDeferred() const { return !deferredQueue_.empty(); }
    bool hasPendingTransactional() const { return !transactionalQueue_.empty(); }
    size_t deferredCount() const { return deferredQueue_.size(); }
    size_t transactionalCount() const { return transactionalQueue_.size(); }

    size_t listenerCount(RuntimeEventType type) const {
        auto it = listeners_.find(static_cast<int>(type));
        return it != listeners_.end() ? it->second.size() : 0;
    }

private:
    void dispatchNow(const RuntimeEvent& event) {
        auto it = listeners_.find(static_cast<int>(event.type));
        if (it != listeners_.end()) {
            // Copy listener list in case a listener unsubscribes during dispatch
            auto copy = it->second;
            for (auto* listener : copy) {
                listener->onRuntimeEvent(event);
            }
        }
    }

    std::unordered_map<int,
        std::vector<RuntimeEventListener*>> listeners_;
    std::vector<RuntimeEvent> deferredQueue_;
    std::vector<RuntimeEvent> transactionalQueue_;
};

}  // namespace morphic

