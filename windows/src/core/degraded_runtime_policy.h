#pragma once

#include "runtime_pressure.h"

namespace morphic {

// Phase 6F: Graceful Degraded-Mode Semantics.
//
// The runtime no longer assumes full operational integrity.
// Under pressure, capability is reduced in bounded stages.
// Degraded mode NEVER corrupts semantic truth.

enum class RuntimeDegradedMode {
    None,                // Full operation
    SuspendDetached,     // Detached surfaces frozen, workspace continues
    SuspendOverlays,     // Overlays suppressed, core surfaces continue
    SerializeRecovery,   // Recovery operations serialized (no concurrent recovery)
    ThrottleTopology,    // Topology mutations rate-limited
    RecoveryOnly         // Only semantic preservation operations permitted
};

// Pressure escalation ladder:
//
// | Pressure State   | Runtime Behavior                          |
// |------------------|-------------------------------------------|
// | Stable           | Full operation                            |
// | Constrained      | Recovery throttling                       |
// | Degraded         | Detached suspension                       |
// | Critical         | Overlay suppression + serialized recovery |
// | RecoveryOnly     | Semantic preservation only                |
class DegradedRuntimePolicy {
public:
    DegradedRuntimePolicy() = default;

    // Evaluate what degraded mode should be active given current pressure.
    RuntimeDegradedMode evaluateMode(RuntimePressureState pressure) const {
        switch (pressure) {
            case RuntimePressureState::Stable:
                return RuntimeDegradedMode::None;
            case RuntimePressureState::Constrained:
                return RuntimeDegradedMode::SerializeRecovery;
            case RuntimePressureState::Degraded:
                return RuntimeDegradedMode::SuspendDetached;
            case RuntimePressureState::Critical:
                return RuntimeDegradedMode::SuspendOverlays;
            case RuntimePressureState::RecoveryOnly:
                return RuntimeDegradedMode::RecoveryOnly;
        }
        return RuntimeDegradedMode::None;
    }

    // Query: is this operation permitted under current degraded mode?
    bool isDetachedOperationPermitted(RuntimeDegradedMode mode) const {
        return mode != RuntimeDegradedMode::SuspendDetached &&
               mode != RuntimeDegradedMode::RecoveryOnly;
    }

    bool isOverlayOperationPermitted(RuntimeDegradedMode mode) const {
        return mode != RuntimeDegradedMode::SuspendOverlays &&
               mode != RuntimeDegradedMode::RecoveryOnly;
    }

    bool isTopologyMutationPermitted(RuntimeDegradedMode mode) const {
        return mode != RuntimeDegradedMode::ThrottleTopology &&
               mode != RuntimeDegradedMode::RecoveryOnly;
    }

    bool isConcurrentRecoveryPermitted(RuntimeDegradedMode mode) const {
        return mode != RuntimeDegradedMode::SerializeRecovery &&
               mode != RuntimeDegradedMode::RecoveryOnly;
    }

    bool isSemanticPreservationOnly(RuntimeDegradedMode mode) const {
        return mode == RuntimeDegradedMode::RecoveryOnly;
    }
};

} // namespace morphic
