#pragma once

#include "../core/types.h"
#include "zombie_auditor.h"
#include "gpu_profiler.h"
#include "thread_activity_auditor.h"
#include "../rendering/renderer_manager.h"
#include <windows.h>
#include <vector>
#include <string>
#include <chrono>

namespace morphic {

// Phase 2A.4 — Zombie Decay Tracker.
//
// Long-horizon idle decay observation with exponential backoff intervals.
// Tracks how zombie engine resources evolve over time after detachment.
//
// Observation windows: 1s, 2s, 5s, 10s, 30s (configurable)
// Total observation: sum of all windows (~48s default)
//
// At each decay point captures:
//   1. Process snapshot (threads, handles, GDI, memory)
//   2. VRAM snapshot (dedicated, shared, budget)
//   3. Per-thread activity attribution (CPU deltas between points)
//   4. State transition markers (phase classification)
//
// State transition detection:
//   Classifies each decay epoch as:
//     - ImmediateCleanup: resources dropping rapidly
//     - GCTurbulence: working set fluctuating (VM compaction)
//     - Stabilizing: deltas shrinking toward zero
//     - Plateau: stable resource level (no further change)
//     - Growing: resources INCREASING (bad signal)
class ZombieDecayTracker {
public:

    // Phase classification for state transitions
    enum class DecayPhase {
        ImmediateCleanup,  // Resources dropping rapidly post-detach
        GCTurbulence,      // Memory fluctuating — VM doing compaction
        Stabilizing,       // Deltas shrinking, approaching steady state
        Plateau,           // Stable — no meaningful change
        Growing,           // Resources increasing — dangerous
        Unknown
    };

    static const char* phaseName(DecayPhase p) {
        switch (p) {
            case DecayPhase::ImmediateCleanup: return "ImmediateCleanup";
            case DecayPhase::GCTurbulence: return "GCTurbulence";
            case DecayPhase::Stabilizing: return "Stabilizing";
            case DecayPhase::Plateau: return "Plateau";
            case DecayPhase::Growing: return "Growing";
            default: return "Unknown";
        }
    }

    // Single decay observation point
    struct DecayPoint {
        int index = 0;
        double elapsedSinceStartMs = 0.0;
        double intervalMs = 0.0;           // Duration of this observation window

        // Process state
        int threadCount = 0;
        int64_t handleCount = 0;
        int64_t gdiObjects = 0;
        int64_t workingSetKB = 0;
        int64_t privateKB = 0;

        // VRAM state
        double dedicatedVramMB = 0.0;
        double sharedVramMB = 0.0;
        double vramBudgetPercent = 0.0;

        // Deltas from previous point
        int threadDelta = 0;
        int64_t handleDelta = 0;
        int64_t workingSetDeltaKB = 0;
        int64_t privateDeltaKB = 0;
        double vramDeltaMB = 0.0;

        // Thread activity during this interval
        int dormantThreads = 0;
        int sporadicThreads = 0;
        int periodicThreads = 0;
        int activeThreads = 0;
        double cpuDeltaMs = 0.0;

        // Phase classification
        DecayPhase phase = DecayPhase::Unknown;

        // Marker string for human-readable transition log
        std::string marker;
    };

    // Full decay tracking result
    struct DecayResult {
        std::string scenario;
        double totalObservationMs = 0.0;
        int zombieCount = 0;

        // All decay points
        std::vector<DecayPoint> curve;

        // Deltas from first to last
        int totalThreadDelta = 0;
        int64_t totalHandleDelta = 0;
        int64_t totalWorkingSetDeltaKB = 0;
        int64_t totalPrivateDeltaKB = 0;
        double totalVramDeltaMB = 0.0;

        // Summary
        bool resourcesDecayed = false;     // Did resources decrease over time?
        bool vramStabilized = false;        // Did VRAM stop growing?
        bool threadsDecayed = false;        // Did thread count decrease?
        int plateauReachedAtIndex = -1;     // First point where plateau was detected

        // Active-to-hidden delta ratio (key sustainability metric)
        double activeToHiddenCpuRatio = 0.0;   // CPU during hidden / CPU during active

        std::string details;
        std::string transitionLog;         // Human-readable phase transition timeline
    };

    ZombieDecayTracker() = default;

    // Run full decay tracking.
    // intervals: observation window durations in ms (exponential backoff)
    // Default: [1000, 2000, 5000, 10000, 30000] = ~48s total
    DecayResult runDecayTracking(
        const RendererManager& mgr,
        const std::vector<int>& intervals = {1000, 2000, 5000, 10000, 30000}) const
    {
        DecayResult result;
        result.scenario = "zombie_decay_tracking";

        // Count zombies
        for (const auto& [id, rec] : mgr.records()) {
            if (rec.isZombie()) result.zombieCount++;
        }

        auto trackingStart = std::chrono::high_resolution_clock::now();

        ZombieAuditor zombieAuditor;
        GpuProfiler gpuProfiler;
        ThreadActivityAuditor threadAuditor;
        bool hasGpu = gpuProfiler.isAvailable();

        DecayPoint previous;  // Track previous for deltas
        bool hasPrevious = false;

        for (size_t i = 0; i < intervals.size(); i++) {
            int intervalMs = intervals[i];

            OutputDebugStringA(("DECAY_TRACKER: Starting interval " +
                std::to_string(i) + " (" + std::to_string(intervalMs) + "ms)\n").c_str());

            // Run thread attribution audit for this interval
            auto threadResult = threadAuditor.runAudit(intervalMs);

            // Capture process snapshot
            auto procSnap = zombieAuditor.captureProcessSnapshot();

            // Capture VRAM snapshot
            GpuProfiler::VramSnapshot vramSnap;
            if (hasGpu) {
                vramSnap = gpuProfiler.captureSnapshot("decay_" + std::to_string(i));
            }

            auto now = std::chrono::high_resolution_clock::now();
            double elapsedMs = std::chrono::duration<double, std::milli>(
                now - trackingStart).count();

            // Build decay point
            DecayPoint dp;
            dp.index = static_cast<int>(i);
            dp.elapsedSinceStartMs = elapsedMs;
            dp.intervalMs = static_cast<double>(intervalMs);

            // Process metrics
            dp.threadCount = procSnap.threadCount;
            dp.handleCount = static_cast<int64_t>(procSnap.handleCount);
            dp.gdiObjects = static_cast<int64_t>(procSnap.gdiObjects);
            dp.workingSetKB = static_cast<int64_t>(procSnap.workingSetBytes / 1024);
            dp.privateKB = static_cast<int64_t>(procSnap.privateBytes / 1024);

            // VRAM
            dp.dedicatedVramMB = vramSnap.dedicatedUsageMB;
            dp.sharedVramMB = vramSnap.sharedUsageMB;
            dp.vramBudgetPercent = vramSnap.dedicatedUsagePercent;

            // Thread activity
            dp.dormantThreads = threadResult.dormantCount;
            dp.sporadicThreads = threadResult.sporadicCount;
            dp.periodicThreads = threadResult.periodicCount;
            dp.activeThreads = threadResult.activeCount;
            dp.cpuDeltaMs = threadResult.totalCpuDeltaMs;

            // Compute deltas from previous
            if (hasPrevious) {
                dp.threadDelta = dp.threadCount - previous.threadCount;
                dp.handleDelta = dp.handleCount - previous.handleCount;
                dp.workingSetDeltaKB = dp.workingSetKB - previous.workingSetKB;
                dp.privateDeltaKB = dp.privateKB - previous.privateKB;
                dp.vramDeltaMB = dp.dedicatedVramMB - previous.dedicatedVramMB;
            }

            // Classify phase
            dp.phase = classifyPhase(dp, hasPrevious ? &previous : nullptr);

            // Build marker string
            dp.marker = "t=" + std::to_string(static_cast<int>(elapsedMs)) + "ms " +
                phaseName(dp.phase);
            if (dp.threadDelta != 0) dp.marker += " threads" + std::to_string(dp.threadDelta);
            if (dp.privateDeltaKB != 0) dp.marker += " mem" +
                std::to_string(dp.privateDeltaKB) + "KB";
            if (dp.activeThreads > 0) dp.marker += " active=" +
                std::to_string(dp.activeThreads);

            result.transitionLog += dp.marker + "\n";

            OutputDebugStringA(("DECAY_TRACKER: " + dp.marker + "\n").c_str());

            result.curve.push_back(dp);
            previous = dp;
            hasPrevious = true;
        }

        // Compute total deltas (first to last)
        if (result.curve.size() >= 2) {
            const auto& first = result.curve.front();
            const auto& last = result.curve.back();

            result.totalThreadDelta = last.threadCount - first.threadCount;
            result.totalHandleDelta = last.handleCount - first.handleCount;
            result.totalWorkingSetDeltaKB = last.workingSetKB - first.workingSetKB;
            result.totalPrivateDeltaKB = last.privateKB - first.privateKB;
            result.totalVramDeltaMB = last.dedicatedVramMB - first.dedicatedVramMB;

            result.totalObservationMs = last.elapsedSinceStartMs;

            // Did resources decay?
            result.resourcesDecayed = (result.totalPrivateDeltaKB < -100);
            result.threadsDecayed = (result.totalThreadDelta < 0);
            result.vramStabilized = (std::abs(result.totalVramDeltaMB) < 1.0);

            // Find plateau index
            for (size_t i = 1; i < result.curve.size(); i++) {
                if (result.curve[i].phase == DecayPhase::Plateau) {
                    result.plateauReachedAtIndex = static_cast<int>(i);
                    break;
                }
            }
        }

        // Build details
        result.details = "Zombies=" + std::to_string(result.zombieCount) +
            " Duration=" + std::to_string(result.totalObservationMs) + "ms" +
            " ThreadDelta=" + std::to_string(result.totalThreadDelta) +
            " PrivateDeltaKB=" + std::to_string(result.totalPrivateDeltaKB) +
            " VRAMDeltaMB=" + std::to_string(result.totalVramDeltaMB) +
            " Decayed=" + (result.resourcesDecayed ? "YES" : "NO") +
            " VRAMStable=" + (result.vramStabilized ? "YES" : "NO");

        if (result.plateauReachedAtIndex >= 0) {
            result.details += " PlateauAt=" +
                std::to_string(result.curve[result.plateauReachedAtIndex].elapsedSinceStartMs) + "ms";
        }

        OutputDebugStringA(("DECAY_TRACKER: COMPLETE " + result.details + "\n").c_str());

        return result;
    }

private:
    DecayPhase classifyPhase(const DecayPoint& current,
                             const DecayPoint* previous) const
    {
        if (!previous) return DecayPhase::Unknown;

        int64_t memDelta = current.privateDeltaKB;
        int64_t wsDelta = current.workingSetDeltaKB;

        // Growing: both memory metrics increasing
        if (memDelta > 500 && wsDelta > 500) {
            return DecayPhase::Growing;
        }

        // ImmediateCleanup: significant memory drop
        if (memDelta < -1000) {
            return DecayPhase::ImmediateCleanup;
        }

        // GCTurbulence: working set fluctuating in opposite direction to private
        if ((wsDelta > 500 && memDelta < -200) || (wsDelta < -500 && memDelta > 200)) {
            return DecayPhase::GCTurbulence;
        }

        // Plateau: very small deltas
        if (std::abs(memDelta) < 200 && std::abs(wsDelta) < 500 &&
            current.threadDelta == 0) {
            return DecayPhase::Plateau;
        }

        // Stabilizing: deltas shrinking
        if (std::abs(memDelta) < std::abs(previous->privateDeltaKB)) {
            return DecayPhase::Stabilizing;
        }

        return DecayPhase::Unknown;
    }
};

}  // namespace morphic
