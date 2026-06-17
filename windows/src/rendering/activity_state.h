#pragma once

namespace morphic {

// Phase 2B — Runtime Activity State
//
// Per-renderer classification of OBSERVABLE runtime behavior.
// Workload-controller-owned. Independent of visibility and lifecycle.
//
// CRITICAL: These are defined by what IS happening, not by what we
// INTENDED to suppress. This avoids assuming control mechanism success.
//
// Observable guarantees (locked definitions — do not reinterpret):
//   Active    = sustained frame cadence observed
//   Throttled = cadence measurably reduced vs Active baseline
//   Parked    = no sustained frame cadence, warm memory retained
//   Dormant   = no meaningful wake activity
//
// THREAD: Read/write on UI thread only.
enum class ActivityState {
    Active,      // sustained frame cadence observed
    Throttled,   // reduced frame cadence observed
    Parked,      // no meaningful frame cadence, warm memory retained
    Dormant,     // no meaningful wake activity
    Destroying,  // being destroyed — blocks all governance interaction
};

inline const char* toString(ActivityState s) {
    switch (s) {
        case ActivityState::Active:     return "Active";
        case ActivityState::Throttled:  return "Throttled";
        case ActivityState::Parked:     return "Parked";
        case ActivityState::Dormant:    return "Dormant";
        case ActivityState::Destroying: return "Destroying";
    }
    return "?";
}

// Phase 2B — Activity Classification (DIAGNOSTIC ONLY)
//
// Telemetry interpretation of renderer behavior quality.
// NOT an operational state. Never used to drive policy directly.
// The FrameCadenceMonitor classifies; the policy engine MAY react,
// but classification and policy are separate concerns.
enum class ActivityClassification {
    Healthy,     // cadence matches expected for current activity state
    Persistent,  // activity continues beyond expected decay
    Runaway,     // hidden + active cadence detected (alert)
    DormantOk,   // genuinely inactive (matches Dormant/Parked expectation)
    Unstable,    // erratic cadence, oscillating
};

inline const char* toString(ActivityClassification c) {
    switch (c) {
        case ActivityClassification::Healthy:    return "Healthy";
        case ActivityClassification::Persistent: return "Persistent";
        case ActivityClassification::Runaway:    return "Runaway";
        case ActivityClassification::DormantOk:  return "DormantOk";
        case ActivityClassification::Unstable:   return "Unstable";
    }
    return "?";
}

}  // namespace morphic
