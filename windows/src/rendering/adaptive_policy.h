#pragma once

#include "system_pressure_monitor.h"
#include "recovery_state.h"
#include "residency_budget.h"
#include "governance_types.h"
#include <chrono>
#include <string>
#include <algorithm>

namespace morphic {

// Phase 3D — Adaptive Policy (Parameter Modulation Layer)
//
// CARDINAL RULE: Adaptation changes PARAMETERS, never LOGIC.
//
// This layer sits ABOVE RuntimePolicy and modulates its parameters
// based on observable system state. RuntimePolicy logic is UNCHANGED.
//
// What this does:
//   - Reads system pressure (memory, CPU, battery)
//   - Reads recovery saturation (concurrent wakes, peak concurrency)
//   - Reads experiential telemetry (P95, instability)
//   - Outputs bounded AdaptiveParameters
//   - All decisions are logged, reversible, and deterministic
//
// What this does NOT do:
//   - Reinterpret invariants
//   - Reinterpret workload semantics
//   - Reinterpret continuity contracts
//   - Change policy LOGIC
//
// It ONLY adjusts: timing, concurrency, aggressiveness, thresholds.
// NOT: meaning.
//
// FAILSAFE: If AdaptivePolicy is removed, RuntimePolicy continues
// to operate correctly with default parameters. AdaptivePolicy is
// optimization, NOT survival authority.
//
// ADAPTATION CADENCE: Parameters recompute at most every 2 seconds.
// This prevents parameter flapping without introducing hysteresis.
// Parameters are computed from CURRENT state, not history.
//
// THREAD: UI thread only.
// ============================================================
//  ABSOLUTE INVARIANTS — AdaptivePolicy may NEVER:
//    - reorder survival tiers
//    - reinterpret workload meaning
//    - block invariant-protected workloads
//    - permanently defer progress
//
//  AdaptivePolicy ONLY:
//    - modulates parameters
//    - influences timing
//    - influences concurrency
//    - influences thresholds
//
//  RuntimePolicy remains sole semantic authority.
// ============================================================

// ============================================================
//  Perceptual Delay Classification
// ============================================================

enum class WakeDelayPerception {
    Imperceptible,  // < 16ms  (sub-frame, user cannot perceive)
    Noticeable,     // 16-50ms (1-3 frames, detectable but acceptable)
    Sluggish,       // 50-200ms (perceptually slow, user notices delay)
    Disruptive      // > 200ms (user-visible failure, unacceptable)
};

inline WakeDelayPerception classifyWakeDelay(double delayMs) {
    if (delayMs < 16.0)  return WakeDelayPerception::Imperceptible;
    if (delayMs < 50.0)  return WakeDelayPerception::Noticeable;
    if (delayMs < 200.0) return WakeDelayPerception::Sluggish;
    return WakeDelayPerception::Disruptive;
}

inline const char* toString(WakeDelayPerception p) {
    switch (p) {
        case WakeDelayPerception::Imperceptible: return "imperceptible";
        case WakeDelayPerception::Noticeable:    return "noticeable";
        case WakeDelayPerception::Sluggish:      return "sluggish";
        case WakeDelayPerception::Disruptive:    return "disruptive";
    }
    return "unknown";
}

// ============================================================
//  Adaptive Parameters
// ============================================================

struct AdaptiveParameters {
    // ---- Park aggressiveness ----
    // Multiplier on debounce timings in RuntimePolicy.
    // 1.0 = normal, 0.5 = twice as aggressive, 2.0 = twice as reluctant.
    // Lower = park sooner. Higher = wait longer before parking.
    float parkAggressiveness = 1.0f;
    static constexpr float kParkAggrMin = 0.3f;
    static constexpr float kParkAggrMax = 3.0f;

    // ---- Wake concurrency limit ----
    // Maximum simultaneous recoveries allowed.
    // Governance will defer additional wakes beyond this limit.
    int maxConcurrentWakes = 6;
    static constexpr int kConcMin = 1;
    static constexpr int kConcMax = 10;

    // ---- Budget pressure threshold shift ----
    // The utilization% at which BudgetPressure becomes Constrained.
    // Lower = more aggressive budget enforcement.
    float budgetConstrainedThreshold = 0.85f;
    static constexpr float kBudgetThreshMin = 0.50f;
    static constexpr float kBudgetThreshMax = 1.0f;

    // ---- Wake stagger interval ----
    // Milliseconds between concurrent wakes.
    // 0 = no staggering (all wake simultaneously).
    // Only activated under pressure; never default behavior.
    int wakeStaggerMs = 0;
    static constexpr int kStaggerMin = 0;
    static constexpr int kStaggerMax = 200;

    // ---- Adaptation source (explainability) ----
    // Human-readable description of why parameters are at current values.
    // This ensures every adaptive state is inspectable.
    const char* adaptationSource = "default";

    // ---- Adaptation level ----
    // Categorical summary of current adaptation intensity.
    enum class Level {
        Default,    // no adaptation active, all parameters at baseline
        Light,      // minor adjustments (battery, moderate pressure)
        Moderate,   // significant adjustments (elevated pressure)
        Aggressive  // maximum adaptation (critical pressure)
    };
    Level level = Level::Default;

    const char* levelName() const {
        switch (level) {
            case Level::Default:    return "default";
            case Level::Light:      return "light";
            case Level::Moderate:   return "moderate";
            case Level::Aggressive: return "aggressive";
        }
        return "unknown";
    }

    // Clamp all parameters to valid ranges.
    void clamp() {
        parkAggressiveness = (std::max)(kParkAggrMin, (std::min)(kParkAggrMax, parkAggressiveness));
        maxConcurrentWakes = (std::max)(kConcMin, (std::min)(kConcMax, maxConcurrentWakes));
        budgetConstrainedThreshold = (std::max)(kBudgetThreshMin, (std::min)(kBudgetThreshMax, budgetConstrainedThreshold));
        wakeStaggerMs = (std::max)(kStaggerMin, (std::min)(kStaggerMax, wakeStaggerMs));
    }

    // Are we at default (no adaptation)?
    bool isDefault() const {
        return level == Level::Default;
    }
};

// ============================================================
//  Adaptive Policy Engine
// ============================================================

class AdaptivePolicy {
public:
    // Compute adaptive parameters from current observable state.
    //
    // Inputs (all read-only, no side effects):
    //   - System pressure (memory, CPU, battery)
    //   - Recovery saturation (concurrent wakes, peak)
    //   - Experiential telemetry (P95, instability)
    //
    // Output: bounded AdaptiveParameters
    //
    // CADENCE: recomputes at most every kAdaptationCadenceMs.
    // Between recomputes, returns cached parameters.
    // This prevents parameter flapping without hysteresis.
    AdaptiveParameters compute(
        const SystemPressureMonitor::Pressure& pressure,
        const RecoverySaturation& saturation,
        const RecoveryExperienceReport& experience
    ) {
        auto now = std::chrono::high_resolution_clock::now();
        auto sinceLastMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - lastComputeTime_).count();

        // Bounded adaptation cadence: skip if too recent
        if (sinceLastMs < kAdaptationCadenceMs && computeCount_ > 0) {
            lastWasCached_ = true;
            return cached_;
        }
        lastWasCached_ = false;

        lastComputeTime_ = now;
        computeCount_++;

        AdaptiveParameters params;

        // ---- LAYER 1: Battery modulation ----
        // Battery changes slowly (polled every 30s).
        // Conservative, predictable adaptation.
        if (pressure.onBattery) {
            if (pressure.batteryPercent < 20.0f) {
                // Critical battery: aggressive conservation
                params.parkAggressiveness = 0.4f;  // park much sooner
                params.maxConcurrentWakes = 2;      // limit wake concurrency
                params.wakeStaggerMs = 100;         // stagger wakes
                params.budgetConstrainedThreshold = 0.55f;
                params.adaptationSource = "battery_critical";
                params.level = AdaptiveParameters::Level::Aggressive;
            } else if (pressure.batteryPercent < 50.0f) {
                // Low battery: moderate conservation
                params.parkAggressiveness = 0.6f;
                params.maxConcurrentWakes = 3;
                params.wakeStaggerMs = 50;
                params.budgetConstrainedThreshold = 0.65f;
                params.adaptationSource = "battery_low";
                params.level = AdaptiveParameters::Level::Moderate;
            } else {
                // On battery but plenty of charge
                params.parkAggressiveness = 0.8f;
                params.maxConcurrentWakes = 4;
                params.wakeStaggerMs = 0;
                params.budgetConstrainedThreshold = 0.75f;
                params.adaptationSource = "battery_normal";
                params.level = AdaptiveParameters::Level::Light;
            }
        }

        // ---- LAYER 2: System pressure modulation ----
        // Memory/CPU pressure can change quickly.
        // Only escalate beyond battery adaptation, never relax it.
        if (pressure.overallPressure >= PressureLevel::Critical) {
            params.parkAggressiveness = (std::min)(params.parkAggressiveness, 0.3f);
            params.maxConcurrentWakes = (std::min)(params.maxConcurrentWakes, 2);
            params.wakeStaggerMs = (std::max)(params.wakeStaggerMs, 100);
            params.budgetConstrainedThreshold = (std::min)(params.budgetConstrainedThreshold, 0.50f);
            params.adaptationSource = "pressure_critical";
            params.level = AdaptiveParameters::Level::Aggressive;
        } else if (pressure.overallPressure >= PressureLevel::Elevated) {
            params.parkAggressiveness = (std::min)(params.parkAggressiveness, 0.6f);
            params.maxConcurrentWakes = (std::min)(params.maxConcurrentWakes, 4);
            params.wakeStaggerMs = (std::max)(params.wakeStaggerMs, 50);
            params.budgetConstrainedThreshold = (std::min)(params.budgetConstrainedThreshold, 0.70f);
            if (params.level < AdaptiveParameters::Level::Moderate) {
                params.adaptationSource = "pressure_elevated";
                params.level = AdaptiveParameters::Level::Moderate;
            }
        } else if (pressure.overallPressure >= PressureLevel::Moderate) {
            // Moderate: minor nudge only
            params.parkAggressiveness = (std::min)(params.parkAggressiveness, 0.8f);
            if (params.level < AdaptiveParameters::Level::Light) {
                params.adaptationSource = "pressure_moderate";
                params.level = AdaptiveParameters::Level::Light;
            }
        }

        // ---- LAYER 3: Recovery saturation modulation ----
        // Only activate wake staggering when saturation risk exists.
        // This is the FIRST real adaptive-pressure signal.
        if (saturation.isConstrained()) {
            // >60% of engines recovering simultaneously
            params.wakeStaggerMs = (std::max)(params.wakeStaggerMs, 75);
            params.maxConcurrentWakes = (std::min)(params.maxConcurrentWakes, 3);
            if (params.level < AdaptiveParameters::Level::Moderate) {
                params.adaptationSource = "saturation_constrained";
                params.level = AdaptiveParameters::Level::Moderate;
            }
        } else if (saturation.isElevated()) {
            // >30% of engines recovering
            params.wakeStaggerMs = (std::max)(params.wakeStaggerMs, 30);
            if (params.level < AdaptiveParameters::Level::Light) {
                params.adaptationSource = "saturation_elevated";
                params.level = AdaptiveParameters::Level::Light;
            }
        }

        // ---- LAYER 4: Experiential feedback modulation ----
        // Only activate when actual perceptual problems are detected.
        // This prevents premature adaptation.
        if (experience.anyExperientialInstability) {
            // Real perceptual failure detected — reduce concurrency
            params.maxConcurrentWakes = (std::min)(params.maxConcurrentWakes, 2);
            params.wakeStaggerMs = (std::max)(params.wakeStaggerMs, 100);
            params.adaptationSource = "experiential_instability";
            params.level = AdaptiveParameters::Level::Aggressive;
        } else if (experience.coldP95 > 50.0 && experience.distributionSamples >= 6) {
            // P95 cold recovery > 50ms — recovery is slow, add stagger
            params.wakeStaggerMs = (std::max)(params.wakeStaggerMs, 50);
            if (params.level < AdaptiveParameters::Level::Light) {
                params.adaptationSource = "recovery_p95_slow";
                params.level = AdaptiveParameters::Level::Light;
            }
        }

        // ---- Finalize: clamp and cache ----
        params.clamp();
        cached_ = params;
        return params;
    }

    // Get the last computed parameters (no recompute).
    const AdaptiveParameters& current() const { return cached_; }

    // Force a recompute on next call (for testing).
    void invalidateCache() { computeCount_ = 0; }

    // How many times has compute() actually run (vs cache hit)?
    int computeCount() const { return computeCount_; }

    // ---- Simulation hooks (for deterministic testing) ----
    // Simulate battery state — overrides real hardware.
    void simulateBattery(bool onBattery, float batteryPercent) {
        simulatedBattery_ = true;
        simOnBattery_ = onBattery;
        simBatteryPercent_ = batteryPercent;
    }

    void clearSimulatedBattery() {
        simulatedBattery_ = false;
    }

    // Apply battery simulation to pressure reading (call before compute).
    SystemPressureMonitor::Pressure applySimulation(
        SystemPressureMonitor::Pressure p
    ) const {
        if (simulatedBattery_) {
            p.onBattery = simOnBattery_;
            p.batteryPercent = simBatteryPercent_;
            // Recompute overall pressure with simulated battery
            if (p.onBattery && p.batteryPercent < 20.0f) {
                if (p.overallPressure < PressureLevel::Elevated) {
                    p.overallPressure = PressureLevel::Elevated;
                }
            }
        }
        return p;
    }

    // Phase 3E: Dormancy audit accessors
    bool wasCached() const { return lastWasCached_; }

private:
    // Adaptation cadence: recompute at most every 2 seconds.
    // Prevents parameter flapping without introducing hysteresis.
    static constexpr int64_t kAdaptationCadenceMs = 2000;

    AdaptiveParameters cached_;
    std::chrono::high_resolution_clock::time_point lastComputeTime_;
    int computeCount_ = 0;
    bool lastWasCached_ = false;   // Phase 3E: true if last compute() returned cached

    // Battery simulation state
    bool simulatedBattery_ = false;
    bool simOnBattery_ = false;
    float simBatteryPercent_ = 100.0f;
};

}  // namespace morphic
