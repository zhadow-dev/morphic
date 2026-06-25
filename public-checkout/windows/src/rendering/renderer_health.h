#pragma once

#include "../core/types.h"
#include <chrono>

namespace morphic {

// Phase 4 — Renderer health monitoring.
//
// Each renderer operates in its own fault domain. Failures MUST NEVER:
//   - collapse workspace topology
//   - corrupt activation graph
//   - poison orchestration runtime
//   - deadlock compositor runtime
//
// RendererHealth tracks the observable state of a renderer backend.
// OrchestrationRuntime and RendererRuntime consume this for scheduling
// and recovery decisions.
//
// THREAD: UI thread only for state mutations.
//         Health checks may be triggered from watchdog timer.
enum class RendererHealth {
    Healthy,        // Producing frames, responding to commands.
                    // Normal operation.

    Degraded,       // Alive but slow or dropping frames.
                    // May indicate GPU pressure, memory thrashing,
                    // or Dart GC storms.

    Hung,           // Not responding to commands.
                    // Watchdog timer expired without frame production.
                    // Engine may be in infinite loop or deadlocked.

    Crashed,        // Process/engine terminated unexpectedly.
                    // HWND may still exist (orphan).
                    // Requires cleanup or restart.

    Recovering      // Restart or recovery in progress.
                    // Engine being recreated, view being reattached.
                    // Not yet producing frames.
};

inline const char* toString(RendererHealth health) {
    switch (health) {
        case RendererHealth::Healthy:    return "healthy";
        case RendererHealth::Degraded:   return "degraded";
        case RendererHealth::Hung:       return "hung";
        case RendererHealth::Crashed:    return "crashed";
        case RendererHealth::Recovering: return "recovering";
    }
    return "unknown";
}

// Fault domain — per-renderer isolation boundary.
//
// Each renderer gets its own fault domain. The runtime monitors health
// independently per renderer so that one crashed engine does not
// affect others.
//
// Future extensions:
//   - watchdog timer per renderer
//   - crash count + exponential backoff
//   - automatic restart policy
//   - surface placeholder fallback (show "crashed" overlay)
//   - frozen surface detection (no frames for N seconds)
struct RendererFaultDomain {
    uint32_t rendererId = 0;
    RendererHealth health = RendererHealth::Healthy;

    // Crash tracking
    int crashCount = 0;
    std::chrono::steady_clock::time_point lastHealthChange{};

    // Watchdog state (future)
    // std::chrono::milliseconds watchdogTimeout{5000};
    // std::chrono::steady_clock::time_point lastFrameTimestamp{};
    // bool watchdogArmed = false;

    bool isOperational() const {
        return health == RendererHealth::Healthy ||
               health == RendererHealth::Degraded;
    }

    bool needsRecovery() const {
        return health == RendererHealth::Hung ||
               health == RendererHealth::Crashed;
    }

    void markHealthy() {
        health = RendererHealth::Healthy;
        lastHealthChange = std::chrono::steady_clock::now();
    }

    void markCrashed() {
        health = RendererHealth::Crashed;
        crashCount++;
        lastHealthChange = std::chrono::steady_clock::now();
    }
};

}  // namespace morphic
