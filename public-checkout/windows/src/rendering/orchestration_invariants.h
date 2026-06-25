#pragma once

#include "activity_state.h"
#include "workload_profile.h"
#include "governance_types.h"
#include "../core/types.h"
#include <string>

namespace morphic {

// Phase 3 — Orchestration Invariants
//
// Non-negotiable safety rules. These are STRUCTURAL constraints,
// not "low scores" or "policy recommendations against."
//
// An invariant violation means the transition is REJECTED
// before policy logic even runs. Policy cannot override invariants.
//
// Invariant categories:
//   1. Workload safety — some surfaces must never be parked/destroyed
//   2. Budget safety — system-wide resource bounds
//   3. Anti-oscillation — churn prevention
//   4. Lifecycle safety — ordering and dependency constraints
class OrchestrationInvariants {
public:
    struct ValidationResult {
        bool allowed = true;
        const char* violatedInvariant = nullptr;
    };

    // Validate whether a transition is allowed.
    // Returns false + reason if ANY invariant is violated.
    ValidationResult validate(
        RenderId id,
        ActivityState currentState,
        ActivityState proposedState,
        const EffectiveWorkloadProfile& profile,
        int recentTransitionCount,     // transitions in last 10s
        double timeSinceLastTransitionSec  // seconds since last transition
    ) const {
        // ---- Invariant 1: InteractionCritical must never leave Active ----
        if (profile.traits.interactionCritical &&
            proposedState != ActivityState::Active) {
            return { false, "INVARIANT: InteractionCritical must remain Active" };
        }

        // ---- Invariant 2: ContinuitySensitive must never be parked ----
        if (profile.traits.continuitySensitive &&
            (proposedState == ActivityState::Parked ||
             proposedState == ActivityState::Dormant)) {
            return { false, "INVARIANT: ContinuitySensitive must not be parked" };
        }

        // ---- Invariant 3: Prohibitive destruction cost must not be destroyed ----
        if (profile.destructionCost == DestructionCost::Prohibitive &&
            proposedState == ActivityState::Dormant) {
            return { false, "INVARIANT: Prohibitive destruction cost prevents dormancy" };
        }

        // ---- Invariant 4: Anti-oscillation — max 3 transitions in 10s ----
        // ASYMMETRIC: wake transitions (→Active) are exempt from churn limit.
        // Rationale: blocking wake = user-visible starvation. Churn limit
        // should prevent over-eager PARKING, not over-eager WAKING.
        if (recentTransitionCount >= maxTransitionsPerWindow_ &&
            proposedState != ActivityState::Active) {
            return { false, "INVARIANT: Transition churn limit exceeded (3/10s)" };
        }

        // ---- Invariant 5: Cooldown — ASYMMETRIC ----
        // Wake (→Active): 200ms cooldown (prevent thrash, not starvation)
        // Park/other: 2s cooldown (prevent optimization churn)
        double requiredCooldown = (proposedState == ActivityState::Active)
            ? wakeCooldownSec_
            : minTransitionCooldownSec_;

        if (timeSinceLastTransitionSec < requiredCooldown) {
            if (proposedState == ActivityState::Active) {
                return { false, "INVARIANT: Wake cooldown not elapsed (200ms)" };
            } else {
                return { false, "INVARIANT: Transition cooldown not elapsed (2s)" };
            }
        }

        // ---- Invariant 6: No backward transitions without wake ----
        // Cannot go Active → Throttled → Active → Throttled rapidly
        // (This is caught by invariants 4+5, but stated explicitly for clarity)

        return { true, nullptr };
    }

private:
    int maxTransitionsPerWindow_ = 3;
    double minTransitionCooldownSec_ = 2.0;   // park/throttle cooldown
    double wakeCooldownSec_ = 0.2;            // wake cooldown (200ms)
};

}  // namespace morphic
