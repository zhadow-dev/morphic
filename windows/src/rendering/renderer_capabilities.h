#pragma once

#include <chrono>

namespace morphic {

// Phase 2B — Renderer Cooperation Levels
//
// Classification of how well a renderer responds to orchestration hints.
// Determines policy aggressiveness — resistant renderers get more
// aggressive treatment than cooperative ones.
enum class CooperationLevel {
    Cooperative,      // responds to all hints, cadence collapses when asked
    SemiCooperative,  // partial throttling, some cadence survives
    Resistant,        // retains activity when hidden despite hints
    Hostile,          // runaway hidden cadence, ignores all hints
};

inline const char* toString(CooperationLevel l) {
    switch (l) {
        case CooperationLevel::Cooperative:      return "Cooperative";
        case CooperationLevel::SemiCooperative:  return "SemiCooperative";
        case CooperationLevel::Resistant:        return "Resistant";
        case CooperationLevel::Hostile:          return "Hostile";
    }
    return "?";
}

// Phase 2B — Renderer Capabilities
//
// Renderers declare what they support. The workload controller queries
// capabilities instead of assuming. This prevents Flutter-specific
// assumptions from contaminating the orchestration core.
//
// RENDERER CONTRACT:
//   RendererAdapter MAY:
//     - send hints
//     - request cadence changes
//     - request parking
//     - report telemetry
//
//   RendererAdapter MAY NOT:
//     - mutate compositor topology
//     - own visibility
//     - alter scheduler authority
//     - bypass policy
struct RendererCapabilities {
    bool supportsCadenceThrottle  = false;  // can reduce frame rate
    bool supportsAnimationPause   = false;  // can pause tickers
    bool supportsPresentationFreeze = false; // can stop presenting
    bool supportsVisibilityHints  = false;  // reacts to visibility changes
    bool supportsWarmParking      = false;  // can park and resume without cold start

    CooperationLevel cooperationLevel = CooperationLevel::Hostile;

    // Expected resume time. Used by policy to weigh parking decisions.
    // Zero means unknown/unmeasured.
    std::chrono::milliseconds estimatedWakeLatency{0};
};

// Phase 2B — Command Result
//
// Renderer refusal semantics. Orchestration is a negotiated protocol,
// NOT a command-and-control engine. Renderers may partially cooperate
// or refuse entirely.
enum class CommandResult {
    Accepted,          // command fully applied
    Ignored,           // renderer did not react
    PartiallyApplied,  // some aspects applied, others not
    Failed,            // command delivery or execution failed
};

inline const char* toString(CommandResult r) {
    switch (r) {
        case CommandResult::Accepted:         return "Accepted";
        case CommandResult::Ignored:          return "Ignored";
        case CommandResult::PartiallyApplied: return "PartiallyApplied";
        case CommandResult::Failed:           return "Failed";
    }
    return "?";
}

// Phase 2B — Freeze Domains
//
// "Freeze" is NOT a single concept. Each domain is independently
// controllable. "Park" = AnimationFrozen + SchedulerFrozen.
// "Dormant" = all frozen.
//
// | Domain              | What Stops            | What Continues            |
// |---------------------|-----------------------|---------------------------|
// | PresentationFrozen  | visible frames        | Dart isolate, timers      |
// | AnimationFrozen     | tickers               | frame callbacks, async    |
// | SchedulerFrozen     | frame scheduling      | isolate, microtasks       |
// | InputFrozen         | mouse, keyboard       | everything else           |
// | RasterFrozen        | GPU rasterization     | CPU, isolate, timers      |
enum class FreezeDomain {
    Presentation,
    Animation,
    Scheduler,
    Input,
    Raster,
};

}  // namespace morphic
