#pragma once

#include "activity_state.h"
#include <vector>
#include <string>

namespace morphic {

// Phase 3 — Policy Explanation (Hierarchical)
//
// Every governance decision is explainable.
// Without explanations, adaptive orchestration becomes:
// black-box instability.
//
// Three tiers:
//   Tier 1 — Dominant reason: the single decisive factor
//   Tier 2 — Modifiers: secondary factors that influenced the decision
//   Tier 3 — Telemetry: low-level observations for deep debugging
//
// Flat factor lists become unreadable telemetry spam.
// Hierarchy preserves signal.
struct PolicyExplanation {
    ActivityState recommended = ActivityState::Active;
    float confidence = 0.0f;

    // ---- Tier 1: Dominant Reason ----
    // The single most important factor. One string. Decisive.
    const char* dominantReason = "default";
    bool dominatedByInvariant = false;   // true = invariant overrode policy logic

    // ---- Tier 2: Modifiers ----
    // Secondary factors that influenced the recommendation.
    // Capped at 5 to prevent noise.
    struct Modifier {
        const char* name = "";
        const char* detail = "";
    };
    static constexpr int kMaxModifiers = 5;
    Modifier modifiers[kMaxModifiers];
    int modifierCount = 0;

    void addModifier(const char* name, const char* detail) {
        if (modifierCount < kMaxModifiers) {
            modifiers[modifierCount++] = { name, detail };
        }
    }

    // ---- Tier 3: Telemetry Notes ----
    // Low-level observations. For deep debugging only.
    // Capped at 10 to prevent explosion.
    static constexpr int kMaxTelemetryNotes = 10;
    const char* telemetryNotes[kMaxTelemetryNotes];
    int telemetryNoteCount = 0;

    void addTelemetry(const char* note) {
        if (telemetryNoteCount < kMaxTelemetryNotes) {
            telemetryNotes[telemetryNoteCount++] = note;
        }
    }

    // ---- Traceability: which layer drove the decision ----
    bool visibilityDriven = false;
    bool pressureDriven = false;
    bool budgetDriven = false;
    bool invariantDriven = false;
    bool workloadDriven = false;

    // Phase 3B.3 v3: Budget authority traceability
    bool budgetEscalated = false;    // true = budget escalated warm → parked
    bool budgetProtected = false;    // true = MustRemainWarm survived budget pressure
};

}  // namespace morphic
