#pragma once

#include "visibility_state.h"
#include <cstdint>

namespace morphic {

// Phase 3 — Observable Context (Facts Only)
//
// CRITICAL DESIGN RULE:
// This struct contains ONLY observable facts.
// NO policy conclusions. NO governance interpretations.
//
//   Context  = "lastInteractionMs = 4500, visibility = Hidden"
//   Policy   = "hidden >5s + high parking affinity → recommend Parked"
//
// These are DIFFERENT layers. Never mix them.
// The policy engine READS context. Context never contains policy output.
struct ObservableContext {
    // ---- Visibility (atomic, not collapsed) ----
    VisibilityState visibility = VisibilityState::Unknown;
    float visibilityConfidence = 0.0f;    // 0=uncertain, 1=certain

    // ---- Interaction (raw observations) ----
    double lastInteractionMs = 0.0;       // ms since last input event on this surface
    int recentInteractionCount = 0;       // interactions in last 30s window

    // ---- Resource observations ----
    float currentWarmMB = 0.0f;           // this renderer's current private memory
    int currentThreadCount = 0;           // threads attributed to this renderer

    // ---- Cadence observations ----
    float currentFps = 0.0f;              // instantaneous frame rate
    int64_t totalFramesRendered = 0;      // lifetime frame count
    int recentWakeCount = 0;              // wake transitions in last 60s

    // ---- Timing observations ----
    double timeInCurrentStateSec = 0.0;   // seconds in current ActivityState
    double lastRecoveryMs = 0.0;          // ms from last resume → first frame (empirical)

    // ---- Lifecycle observations ----
    int lifetimeParkResumeCycles = 0;     // total park→resume transitions
    double totalParkedSec = 0.0;          // cumulative seconds spent parked
};

}  // namespace morphic
