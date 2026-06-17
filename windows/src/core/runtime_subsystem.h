#pragma once

namespace morphic {

// Phase 4 — Subsystem dependency law.
//
// RuntimeKernel owns all subsystems. But subsystems must NOT
// freely cross-reference each other. Without dependency direction
// rules, the event bus becomes fake decoupling and RuntimeKernel
// becomes "Compositor 2.0" — a god object one level higher.
//
// ALLOWED dependency directions are defined per subsystem.
// FORBIDDEN coupling is as important as allowed coupling.
//
// ═══════════════════════════════════════════════════════════════
// DEPENDENCY DIRECTION LAW
// ═══════════════════════════════════════════════════════════════
//
// Layer 0 (Foundation — no runtime dependencies):
//   RuntimePhase, RuntimeCapabilities, RuntimeThread, types.h
//
// Layer 1 (Data — depends only on Layer 0):
//   SurfaceRegistry, SceneGraph
//     MAY use: types, RuntimePhase
//     MAY NOT: query any other subsystem
//
// Layer 2 (Policy — depends on Layer 0-1):
//   TopologyManager, ActivationManager, FocusGraph
//     MAY use: SurfaceRegistry (read surface state)
//     MAY use: RuntimeCapabilities (feature checks)
//     MAY emit: RuntimeEvents
//     MAY NOT: query RendererRuntime
//     MAY NOT: query OrchestrationRuntime
//     MAY NOT: mutate each other's state
//
// Layer 3 (Orchestration — depends on Layer 0-2):
//   RendererRuntime, OrchestrationRuntime
//     MAY use: SurfaceRegistry (read surface state)
//     MAY use: RendererRuntime ← OrchestrationRuntime only
//     MAY emit: RuntimeEvents
//     MAY NOT: mutate topology
//     MAY NOT: mutate activation
//     MAY NOT: mutate focus
//
// Layer 4 (Coordination — may reference any layer):
//   Compositor (frame scheduling, transactions)
//   InputRouter (accelerator dispatch)
//     MAY delegate to any subsystem via well-typed calls
//     MAY NOT: bypass subsystem authority
//       (e.g., Compositor may NOT call SetWindowPos directly)
//
// Layer 5 (Integration — method channel bridge):
//   MorphicPlugin / MethodDispatch
//     MAY call Compositor/SurfaceRegistry public APIs
//     MAY NOT: reach into subsystem internals
//
// ═══════════════════════════════════════════════════════════════
// CROSS-SUBSYSTEM COMMUNICATION LAW
// ═══════════════════════════════════════════════════════════════
//
// Same-layer communication:    via RuntimeEventBus ONLY
// Downward communication:      via direct API calls (allowed)
// Upward communication:        via RuntimeEventBus ONLY
// Lateral communication:       FORBIDDEN (use events)
//
// Examples:
//   TopologyManager → SurfaceRegistry:    OK (downward, Layer 2→1)
//   TopologyManager → ActivationManager:  FORBIDDEN (lateral, Layer 2→2)
//   ActivationManager → FocusGraph:       FORBIDDEN (lateral, Layer 2→2)
//   OrchestrationRuntime → RendererRuntime: OK (same layer, explicit)
//   RendererRuntime → TopologyManager:    FORBIDDEN (upward)
//   SurfaceRegistry → emit SurfaceCreated: OK (event, any listener)
//
// ═══════════════════════════════════════════════════════════════
// MUTATION AUTHORITY LAW
// ═══════════════════════════════════════════════════════════════
//
// Each resource has EXACTLY ONE mutation authority:
//
//   Resource              | Authority           | Everyone else
//   ----------------------|---------------------|------------------
//   Surface instances     | SurfaceRegistry     | read-only
//   HWND style/topology   | TopologyManager     | FORBIDDEN
//   Z-order / activation  | ActivationManager   | FORBIDDEN
//   Semantic focus        | FocusGraph          | read-only
//   Renderer lifecycle    | RendererRuntime      | read-only
//   Workload state        | OrchestrationRuntime | read-only
//   Workspace membership  | WorkspaceController | read-only
//   Frame scheduling      | Compositor          | request only
//

// ═══════════════════════════════════════════════════════════════
// CONSTITUTIONAL FREEZE & BOUNDARY LAWS
// ═══════════════════════════════════════════════════════════════
//
// 1. NO NEW CORE TYPES POLICY
//    No new kernel-level nouns (e.g., RuntimeHealthLayer, SemanticRecoveryDomain,
//    TemporalRepairPolicy, etc.) or core architectural types may be introduced
//    without a formal constitutional amendment. The vocabulary of the execution
//    kernel is frozen.
//
// 2. REPLAY PASSIVE BOUNDARY LAW
//    The replay engine owns ZERO authority. It must remain a passive observer,
//    comparator, and injector only. Replay must never:
//      - Decide convergence or bypass normal commit cycle phases.
//      - Direct realization on physical window targets.
//      - Drive parallel runtime evolution or alternative convergence logic.


// Subsystem identity — used for dependency validation and debug logging.
enum class RuntimeSubsystem {
    // Layer 0: Foundation
    Core,

    // Layer 1: Data
    SurfaceRegistry,

    // Layer 2: Policy
    Topology,
    Activation,
    Focus,

    // Layer 3: Orchestration
    Rendering,
    Orchestration,

    // Layer 4: Coordination
    Composition,
    Input,

    // Layer 5: Integration
    MethodBridge,
};

inline const char* toString(RuntimeSubsystem s) {
    switch (s) {
        case RuntimeSubsystem::Core:            return "core";
        case RuntimeSubsystem::SurfaceRegistry: return "surfaceRegistry";
        case RuntimeSubsystem::Topology:        return "topology";
        case RuntimeSubsystem::Activation:      return "activation";
        case RuntimeSubsystem::Focus:           return "focus";
        case RuntimeSubsystem::Rendering:       return "rendering";
        case RuntimeSubsystem::Orchestration:   return "orchestration";
        case RuntimeSubsystem::Composition:     return "composition";
        case RuntimeSubsystem::Input:           return "input";
        case RuntimeSubsystem::MethodBridge:    return "methodBridge";
    }
    return "unknown";
}

}  // namespace morphic
