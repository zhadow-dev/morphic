#ifndef RUNNER_ENGINE_STATE_H_
#define RUNNER_ENGINE_STATE_H_

namespace morphic {

// ENGINE RETENTION — PHASE R1. The engine's lifecycle, distinct from the
// surface's TeardownState. The R0 finding reframed the target: DESTRUCTION is the
// hazardous operation, not retained lifetime — so a closed surface's engine is
// RETAINED (DORMANT), not destroyed. Destroys happen only under pool-pressure
// eviction (RETIRING) or, with R0, never at exit.
//
// SACRED BOUNDARY (doc/ENGINE_RETENTION_POOL_DESIGN.md): DORMANT != CLEAN !=
// RESET != REUSABLE. A dormant engine is a SUSPENDED ORIGINAL — retained,
// identity-preserving, quarantined, lifecycle-suspended — NOT a sanitized blank.
// There is intentionally NO state here for "reusable": reuse is Phase R2 and must
// pass through an explicit SANITIZING gate that does not yet exist. A dormant
// engine must stay semantically dead: no shell presence, Alt-Tab, preview,
// activation, focus, rendering, or capture participation.
enum class EngineState {
  kLive = 0,   // bound to an on-screen surface (the normal case; not pooled)
  kDormant,    // surface gone; engine retained, parked, cloaked, off-shell, idle
  kRetiring,   // selected for destruction (pool pressure) — the rare crash window
  kDestroyed,  // terminal
};

// R1 master toggle (reversible): false restores the legacy destroy-on-close path
// (every close → serialized reaper → reset, the during-session ~4% crash). true
// retains closed engines dormant and destroys only under pool pressure.
inline constexpr bool kEngineRetention = true;

// Fixed hard cap on dormant engines (NOT adaptive — determinism > optimization
// while stabilizing). Env-overridable (MORPHIC_DORMANT_CAP) for testing eviction.
inline constexpr long kDormantCapDefault = 12;

inline const char* EngineStateName(EngineState s) {
  switch (s) {
    case EngineState::kLive: return "LIVE";
    case EngineState::kDormant: return "DORMANT";
    case EngineState::kRetiring: return "RETIRING";
    case EngineState::kDestroyed: return "DESTROYED";
  }
  return "?";
}

}  // namespace morphic

#endif  // RUNNER_ENGINE_STATE_H_
