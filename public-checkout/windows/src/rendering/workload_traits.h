#pragma once

namespace morphic {

// Phase 3 — Workload Traits (Compositional)
//
// CRITICAL DESIGN RULE:
// Workloads are compositional, NOT categorical.
// A surface can be simultaneously: animationSensitive + memoryHeavy.
// An enum like "WorkloadClass::AnimatedImageHeavy" does NOT scale.
// Bool traits DO scale.
//
// These traits describe WHAT a workload IS.
// Governance types (WakePriority, ParkingAffinity) describe
// HOW it should be treated. Keep these concerns separate.
struct WorkloadTraits {
    // Cadence traits
    bool animationSensitive = false;    // needs smooth frame continuity on resume
    bool cadenceIntensive = false;      // sustained high-fps rendering expected

    // Responsiveness traits
    bool latencyCritical = false;       // interaction response time matters
    bool interactionCritical = false;   // must be instantly responsive, never park

    // Resource traits
    bool memoryHeavy = false;           // large textures, image caches, significant heap
    bool backgroundCompute = false;     // pure computation, no visible output needed

    // Continuity traits
    bool continuitySensitive = false;   // audio/video playback must not gap

    // Lifecycle traits
    bool ephemeral = false;             // transient surface (tooltip, popup), destroy-friendly

    // Convenience: is this workload safe to park?
    bool canPark() const {
        return !interactionCritical && !continuitySensitive;
    }

    // Convenience: is this workload safe to destroy?
    bool canDestroy() const {
        return ephemeral && !interactionCritical && !continuitySensitive;
    }
};

}  // namespace morphic
