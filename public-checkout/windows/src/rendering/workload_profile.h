#pragma once

#include "workload_traits.h"
#include "governance_types.h"

namespace morphic {

// Phase 3 — Workload Profiles (Dual-Authority)
//
// Two-layer design:
//   DeclaredWorkloadProfile  = what the renderer says about itself (untrusted)
//   EffectiveWorkloadProfile = what the orchestrator actually uses (authoritative)
//
// WHY dual-authority matters:
//   - Buggy renderer may mis-declare traits
//   - Malicious plugin could game priority
//   - Stale declaration doesn't match current behavior
//   - Host has system-wide context the renderer lacks
//
// The host computes EffectiveWorkloadProfile from the declared profile
// plus its own observations and policy overrides.

// What the renderer declares about itself.
// Received via method channel from Dart. NOT trusted by orchestration.
struct DeclaredWorkloadProfile {
    WorkloadTraits traits;

    // Self-reported expectations (informational, not authoritative)
    float expectedActiveFps = 60.0f;
    float staleFrameToleranceSec = 0.0f;  // how long stale content is acceptable
    float estimatedWarmMB = 117.0f;       // from empirical Phase 2B data
    float estimatedColdStartMs = 200.0f;  // expected cold start latency
};

// What the orchestrator actually uses for governance decisions.
// Host-authoritative. May differ from declared profile.
struct EffectiveWorkloadProfile {
    WorkloadTraits traits;

    // Governance bands (host-determined, not floats)
    WakePriority wakePriority = WakePriority::Normal;
    ParkingAffinity parkingAffinity = ParkingAffinity::Normal;
    DestructionCost destructionCost = DestructionCost::Moderate;
    PersistenceValue persistenceValue = PersistenceValue::Moderate;

    // Phase 3B.3: Economic survivability ordering.
    // Deterministic priority — NOT float scoring.
    // Under budget pressure, lower tiers get parked first.
    enum class SurvivalTier {
        MustRemainWarm,     // interactionCritical, continuitySensitive
        PreferWarm,         // animationSensitive, latencyCritical
        ParkingCandidate,   // normal, memoryHeavy (parkable under pressure)
        EvictionCandidate   // ephemeral, backgroundCompute (park first)
    };
    SurvivalTier survivalTier = SurvivalTier::ParkingCandidate;

    // Resource estimates (from empirical data, not renderer claims)
    float estimatedWarmMB = 117.0f;
    float estimatedRecoveryMs = 70.0f;

    // Override tracking
    bool hostOverridden = false;
    const char* overrideReason = nullptr;

    // Compute effective profile from declared profile.
    // Host applies its own policy to translate traits → governance bands.
    static EffectiveWorkloadProfile fromDeclared(
        const DeclaredWorkloadProfile& declared)
    {
        EffectiveWorkloadProfile eff;
        eff.traits = declared.traits;
        eff.estimatedWarmMB = declared.estimatedWarmMB;
        eff.estimatedRecoveryMs = declared.estimatedColdStartMs;

        // Translate traits → governance bands (deterministic rules)
        if (declared.traits.interactionCritical) {
            eff.wakePriority = WakePriority::Critical;
            eff.parkingAffinity = ParkingAffinity::Never;
            eff.destructionCost = DestructionCost::Prohibitive;
            eff.persistenceValue = PersistenceValue::Essential;
            eff.survivalTier = SurvivalTier::MustRemainWarm;
        } else if (declared.traits.continuitySensitive) {
            eff.wakePriority = WakePriority::Critical;
            eff.parkingAffinity = ParkingAffinity::Never;
            eff.destructionCost = DestructionCost::Expensive;
            eff.persistenceValue = PersistenceValue::High;
            eff.survivalTier = SurvivalTier::MustRemainWarm;
        } else if (declared.traits.animationSensitive) {
            eff.wakePriority = WakePriority::High;
            eff.parkingAffinity = ParkingAffinity::Reluctant;
            eff.destructionCost = DestructionCost::Moderate;
            eff.persistenceValue = PersistenceValue::Moderate;
            eff.survivalTier = SurvivalTier::PreferWarm;
        } else if (declared.traits.ephemeral) {
            eff.wakePriority = WakePriority::Low;
            eff.parkingAffinity = ParkingAffinity::Eager;
            eff.destructionCost = DestructionCost::Trivial;
            eff.persistenceValue = PersistenceValue::Disposable;
            eff.survivalTier = SurvivalTier::EvictionCandidate;
        } else if (declared.traits.memoryHeavy) {
            eff.wakePriority = WakePriority::Normal;
            eff.parkingAffinity = ParkingAffinity::Normal;
            eff.destructionCost = DestructionCost::Expensive;
            eff.persistenceValue = PersistenceValue::Moderate;
            eff.survivalTier = SurvivalTier::ParkingCandidate;
        } else if (declared.traits.backgroundCompute) {
            eff.wakePriority = WakePriority::Low;
            eff.parkingAffinity = ParkingAffinity::Normal;
            eff.destructionCost = DestructionCost::Moderate;
            eff.persistenceValue = PersistenceValue::Low;
            eff.survivalTier = SurvivalTier::EvictionCandidate;
        }
        // Default: ParkingCandidate (already set by member initializers)

        return eff;
    }
};

}  // namespace morphic
