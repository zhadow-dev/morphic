#pragma once

#include "visibility_state.h"
#include "activity_state.h"
#include "renderer_capabilities.h"
#include "observable_context.h"
#include "workload_profile.h"
#include "policy_explanation.h"
#include "orchestration_invariants.h"
#include "residency_budget.h"
#include "governance_types.h"
#include <chrono>
#include <string>

namespace morphic {

// Phase 3 — Runtime Policy (Upgraded)
//
// Recommends an ActivityState based on:
//   - Observable context (facts)
//   - Effective workload profile (governance bands)
//   - Residency budget (bounded decisions)
//   - Invariants (non-negotiable constraints)
//   - Renderer capabilities (what it can do)
//
// Design principles:
//   - Deterministic: same inputs always produce same output
//   - Explainable: every decision produces a PolicyExplanation
//   - Inspectable: governance bands, not opaque floats
//   - Bounded: budget-aware, never unbounded
//   - Invariant-first: safety rules override all policy logic
//
// THREAD: UI thread only.
class RuntimePolicy {
public:
    // Phase 3: Full governance evaluation.
    // Returns a PolicyExplanation with hierarchical reasoning.
    PolicyExplanation evaluate(
        const ObservableContext& context,
        const EffectiveWorkloadProfile& profile,
        const ResidencyBudget& budget,
        const OrchestrationInvariants& invariants,
        const RendererCapabilities& capabilities,
        ActivityState currentActivity,
        RenderId rendererId,
        int recentTransitionCount,
        double timeSinceLastTransitionSec
    ) const {
        PolicyExplanation expl;
        expl.recommended = currentActivity;  // default: no change
        expl.confidence = 0.0f;

        // ---- LAYER 1: Visibility floor (always evaluated) ----
        // Visible surfaces are ALWAYS Active. Non-negotiable.
        if (context.visibility == VisibilityState::Visible ||
            context.visibility == VisibilityState::PartiallyVisible) {
            expl.recommended = ActivityState::Active;
            expl.confidence = 1.0f;
            expl.dominantReason = "visible";
            expl.visibilityDriven = true;
            return expl;
        }

        // Low visibility confidence → stay conservative.
        if (context.visibilityConfidence < 0.5f) {
            expl.recommended = ActivityState::Active;
            expl.confidence = context.visibilityConfidence;
            expl.dominantReason = "low visibility confidence, staying active";
            expl.visibilityDriven = true;
            expl.addTelemetry("visibilityConfidence < 0.5");
            return expl;
        }

        // ---- LAYER 2: Workload invariant floor ----
        // InteractionCritical, ContinuitySensitive → never leave Active.
        if (profile.parkingAffinity == ParkingAffinity::Never) {
            expl.recommended = ActivityState::Active;
            expl.confidence = 1.0f;
            expl.dominantReason = "workload parking affinity = Never";
            expl.workloadDriven = true;
            expl.invariantDriven = true;
            expl.dominatedByInvariant = true;
            return expl;
        }

        // ---- LAYER 3: Visibility-based policy (with profile timing) ----
        ActivityState desired = ActivityState::Active;
        const char* reason = "default";

        switch (context.visibility) {
            case VisibilityState::Background:
                if (context.timeInCurrentStateSec >= debounceBackgroundSec_ &&
                    capabilities.supportsCadenceThrottle) {
                    desired = ActivityState::Throttled;
                    reason = "background > debounce, throttling";
                } else {
                    reason = "background, within debounce";
                }
                expl.visibilityDriven = true;
                break;

            case VisibilityState::FullyOccluded:
                if (context.timeInCurrentStateSec >= debounceOccludedSec_ &&
                    capabilities.supportsCadenceThrottle) {
                    desired = ActivityState::Throttled;
                    reason = "fully occluded > debounce, throttling";
                } else {
                    reason = "occluded, within debounce";
                }
                expl.visibilityDriven = true;
                break;

            case VisibilityState::Hidden:
                if (context.timeInCurrentStateSec >= parkDebounceForProfile(profile) &&
                    capabilities.supportsWarmParking) {
                    desired = ActivityState::Parked;
                    reason = "hidden > park debounce, parking";
                } else if (capabilities.supportsCadenceThrottle) {
                    desired = ActivityState::Throttled;
                    reason = "hidden, throttling";
                }
                expl.visibilityDriven = true;
                break;

            case VisibilityState::Minimized:
                if (capabilities.supportsWarmParking) {
                    desired = ActivityState::Parked;
                    reason = "minimized, parking";
                } else if (capabilities.supportsCadenceThrottle) {
                    desired = ActivityState::Throttled;
                    reason = "minimized, throttling";
                }
                expl.visibilityDriven = true;
                break;

            case VisibilityState::Detached:
                if (capabilities.supportsWarmParking) {
                    desired = ActivityState::Parked;
                    reason = "detached, parking";
                }
                expl.visibilityDriven = true;
                break;

            default:
                reason = "unknown visibility, staying active";
                break;
        }

        // ---- LAYER 4: Budget pressure escalation ----
        // Economic recommendation escalation, NOT imperative override.
        // Budget influences ALL warm states (Active + Throttled).
        // Warm residency = Active | Throttled. NOT Parked | Dormant.
        //
        // Escalation semantics per tier:
        //   Relaxed     → no budget influence
        //   Elevated    → informational modifier only
        //   Constrained → EvictionCandidate → Parked
        //                  ParkingCandidate → Parked (if hidden/background)
        //                  PreferWarm       → stays warm
        //                  MustRemainWarm   → NEVER budget-evicted
        //   Critical    → park everything except MustRemainWarm
        auto budgetPressure = budget.pressure();

        if (budgetPressure != ResidencyBudget::BudgetPressure::Relaxed &&
            (desired == ActivityState::Active || desired == ActivityState::Throttled)) {

            using ST = EffectiveWorkloadProfile::SurvivalTier;
            using BP = ResidencyBudget::BudgetPressure;

            if (budgetPressure == BP::Critical) {
                // Critical: park everything except MustRemainWarm
                if (profile.survivalTier == ST::MustRemainWarm) {
                    // Protected — budget cannot evict MustRemainWarm
                    expl.addModifier("budgetProtected", "MustRemainWarm survives Critical");
                    expl.budgetProtected = true;
                } else {
                    desired = ActivityState::Parked;
                    reason = "BUDGET CRITICAL: evicting non-essential";
                    expl.budgetDriven = true;
                    expl.budgetEscalated = true;
                    expl.addModifier("budget", "critical pressure, survival tier too low");
                }
            } else if (budgetPressure == BP::Constrained) {
                // Constrained: escalate low-tier warm renderers to Parked
                if (profile.survivalTier == ST::EvictionCandidate) {
                    desired = ActivityState::Parked;
                    reason = "over budget, eviction candidate parked";
                    expl.budgetDriven = true;
                    expl.budgetEscalated = true;
                    expl.addModifier("budget", "constrained, eviction tier");
                } else if (profile.survivalTier == ST::ParkingCandidate &&
                           profile.parkingAffinity != ParkingAffinity::Never) {
                    desired = ActivityState::Parked;
                    reason = "over budget, parking candidate parked";
                    expl.budgetDriven = true;
                    expl.budgetEscalated = true;
                    expl.addModifier("budget", "constrained, parking tier");
                } else if (profile.survivalTier == ST::MustRemainWarm) {
                    expl.addModifier("budgetProtected", "MustRemainWarm survives Constrained");
                    expl.budgetProtected = true;
                }
            } else if (budgetPressure == BP::Elevated) {
                // Elevated: informational only
                if (profile.survivalTier >= ST::ParkingCandidate) {
                    expl.addModifier("budgetElevated", "approaching limits");
                }
            }
        }

        // ---- LAYER 5: Invariant validation ----
        // Invariants have FINAL say. Policy cannot override.
        auto validation = invariants.validate(
            rendererId, currentActivity, desired, profile,
            recentTransitionCount, timeSinceLastTransitionSec);

        if (!validation.allowed) {
            expl.recommended = currentActivity;  // stay put
            expl.confidence = 1.0f;
            expl.dominantReason = validation.violatedInvariant;
            expl.invariantDriven = true;
            expl.dominatedByInvariant = true;
            return expl;
        }

        // ---- Finalize ----
        expl.recommended = desired;
        expl.confidence = 0.9f;
        expl.dominantReason = reason;

        // Add context modifiers
        if (context.lastInteractionMs < 5000.0) {
            expl.addModifier("recentInteraction",
                "last interaction < 5s ago");
        }
        if (budget.utilization() > 0.7f) {
            expl.addModifier("budgetPressure",
                "warm budget > 70%");
        }

        return expl;
    }

    // Phase 2B compatibility: simple evaluation (kept for existing code paths).
    struct Recommendation {
        ActivityState desiredState = ActivityState::Active;
        float confidence = 0.0f;
        const char* reason = "default";
    };

    Recommendation evaluate(
        VisibilityState visibility,
        float visibilityConfidence,
        const RendererCapabilities& capabilities,
        ActivityState currentActivity,
        std::chrono::milliseconds timeInCurrentVisibility
    ) const {
        double timeSec = timeInCurrentVisibility.count() / 1000.0;

        ObservableContext ctx;
        ctx.visibility = visibility;
        ctx.visibilityConfidence = visibilityConfidence;
        ctx.timeInCurrentStateSec = timeSec;

        EffectiveWorkloadProfile defaultProfile;
        ResidencyBudget defaultBudget;
        OrchestrationInvariants defaultInvariants;

        auto expl = evaluate(ctx, defaultProfile, defaultBudget,
                             defaultInvariants, capabilities,
                             currentActivity, 0, 0, 999.0);

        return { expl.recommended, expl.confidence, expl.dominantReason };
    }

private:
    // Debounce timings
    double debounceBackgroundSec_ = 1.0;
    double debounceOccludedSec_ = 2.0;

    // Profile-aware park debounce.
    // Eager parking → shorter debounce. Reluctant → longer.
    double parkDebounceForProfile(const EffectiveWorkloadProfile& profile) const {
        switch (profile.parkingAffinity) {
            case ParkingAffinity::Eager:     return 1.0;   // park quickly
            case ParkingAffinity::Normal:    return 5.0;   // standard 5s
            case ParkingAffinity::Reluctant: return 15.0;  // wait longer
            case ParkingAffinity::Never:     return 1e9;   // effectively never
        }
        return 5.0;
    }
};

}  // namespace morphic
