#pragma once

namespace morphic {

// Phase 3 — Governance Semantic Bands
//
// CRITICAL DESIGN RULE:
// Policy-facing values use coarse semantic bands, NOT floats.
// "wakeUrgency = 0.63" means nothing.
// "WakePriority::High" means something.
//
// These bands are the vocabulary of governance decisions.
// They are inspectable, debuggable, and stable across versions.

// How urgently this renderer must resume after parking.
enum class WakePriority {
    Low,       // can wait 200ms+ to resume (idle dashboard, background)
    Normal,    // should resume within 100ms (standard surface)
    High,      // should resume within 50ms (animated, recently interacted)
    Critical,  // must resume immediately, 0ms delay (interaction-critical)
};

inline const char* toString(WakePriority p) {
    switch (p) {
        case WakePriority::Low:      return "Low";
        case WakePriority::Normal:   return "Normal";
        case WakePriority::High:     return "High";
        case WakePriority::Critical: return "Critical";
    }
    return "?";
}

// How eagerly the orchestrator should park this renderer.
enum class ParkingAffinity {
    Eager,     // park as soon as hidden (ephemeral, disposable)
    Normal,    // park after standard debounce (5s hidden)
    Reluctant, // park only under budget pressure (animated, recently active)
    Never,     // never park (interaction-critical, active media)
};

inline const char* toString(ParkingAffinity a) {
    switch (a) {
        case ParkingAffinity::Eager:     return "Eager";
        case ParkingAffinity::Normal:    return "Normal";
        case ParkingAffinity::Reluctant: return "Reluctant";
        case ParkingAffinity::Never:     return "Never";
    }
    return "?";
}

// How expensive it is to destroy and recreate this renderer.
enum class DestructionCost {
    Trivial,      // cheap to recreate (empty surface, tooltip)
    Moderate,     // some startup cost (standard Flutter engine ~200ms)
    Expensive,    // significant state to rebuild (image caches, scroll position)
    Prohibitive,  // must not destroy (unsaved state, active computation)
};

inline const char* toString(DestructionCost c) {
    switch (c) {
        case DestructionCost::Trivial:     return "Trivial";
        case DestructionCost::Moderate:    return "Moderate";
        case DestructionCost::Expensive:   return "Expensive";
        case DestructionCost::Prohibitive: return "Prohibitive";
    }
    return "?";
}

// How valuable it is to keep this renderer warm in memory.
enum class PersistenceValue {
    Disposable,  // can be destroyed anytime (overlay, transient popup)
    Low,         // slight preference to keep warm
    Moderate,    // meaningful cost to lose (standard working surface)
    High,        // important to preserve (recently edited, active workflow)
    Essential,   // must not lose (unsaved work, critical process)
};

inline const char* toString(PersistenceValue v) {
    switch (v) {
        case PersistenceValue::Disposable: return "Disposable";
        case PersistenceValue::Low:        return "Low";
        case PersistenceValue::Moderate:   return "Moderate";
        case PersistenceValue::High:       return "High";
        case PersistenceValue::Essential:  return "Essential";
    }
    return "?";
}

// System-wide resource pressure level.
enum class PressureLevel {
    Relaxed,   // <50% utilization, no constraints
    Moderate,  // 50-75%, begin conservative parking
    Elevated,  // 75-90%, aggressive parking, reduce warm count
    Critical,  // >90%, emergency eviction, minimize warm residency
};

inline const char* toString(PressureLevel l) {
    switch (l) {
        case PressureLevel::Relaxed:  return "Relaxed";
        case PressureLevel::Moderate: return "Moderate";
        case PressureLevel::Elevated: return "Elevated";
        case PressureLevel::Critical: return "Critical";
    }
    return "?";
}

}  // namespace morphic
