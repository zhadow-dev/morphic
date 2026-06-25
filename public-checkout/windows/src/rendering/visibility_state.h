#pragma once

namespace morphic {

// Phase 2B — Visibility State
//
// Per-surface classification of OS-level visibility.
// Compositor-owned. Independent of renderer activity or lifecycle.
//
// IMPORTANT: Detached is TOPOLOGY state, not runtime state.
// A detached renderer may still be Active, Parked, or Prewarmed.
//
// THREAD: Read/write on UI thread only.
enum class VisibilityState {
    Visible,            // on-screen, not covered
    PartiallyVisible,   // partially behind other windows
    FullyOccluded,      // exists but entirely covered
    Hidden,             // hideWithoutDestroy() called
    Minimized,          // OS-level minimize
    Background,         // app not foreground
    Detached,           // unbound from surface (topology, NOT runtime)
    Unknown,            // initial state before first observation
};

inline const char* toString(VisibilityState s) {
    switch (s) {
        case VisibilityState::Visible:          return "Visible";
        case VisibilityState::PartiallyVisible: return "PartiallyVisible";
        case VisibilityState::FullyOccluded:    return "FullyOccluded";
        case VisibilityState::Hidden:           return "Hidden";
        case VisibilityState::Minimized:        return "Minimized";
        case VisibilityState::Background:       return "Background";
        case VisibilityState::Detached:         return "Detached";
        case VisibilityState::Unknown:          return "Unknown";
    }
    return "?";
}

}  // namespace morphic
