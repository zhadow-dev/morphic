#pragma once

#include "../rendering/renderer_manager.h"
#include "../rendering/activity_state.h"
#include "../rendering/visibility_state.h"
#include "../debug/frame_cadence_monitor.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <functional>

namespace morphic {

// Phase 2B.1 — Sustainability Benchmark Engine
//
// Automated test runner that collects timestamped telemetry snapshots
// at regular intervals. Outputs CSV-formatted data for analysis.
//
// NOT a compositor subsystem. Standalone utility called by the plugin.
//
// THREAD: UI thread only (uses SetTimer).
class SustainabilityBenchmark {
public:
    // Single point-in-time telemetry snapshot.
    struct Snapshot {
        double timestampSec = 0.0;   // seconds since test start
        int64_t privateKB = 0;
        int64_t workingSetKB = 0;
        int32_t threadCount = 0;
        int32_t handleCount = 0;
        int32_t gdiObjects = 0;
        int32_t userObjects = 0;

        // Per-renderer state (aggregate)
        int32_t rendererTotal = 0;
        int32_t rendererActive = 0;   // ActivityState::Active
        int32_t rendererParked = 0;   // ActivityState::Parked
        int32_t rendererThrottled = 0;
        int32_t rendererDormant = 0;

        // Frame cadence (aggregate across all engines)
        int64_t totalFramesRendered = 0;
        int64_t frameDeltaSinceLastSnapshot = 0;

        // Phase label (for CSV annotation)
        std::string phase;

        // Cadence-specific (from FrameCadenceMonitor)
        int64_t cadenceTotalFrames = 0;    // true Dart-reported frame count
        int64_t cadenceHiddenFrames = 0;   // frames produced while hidden/parked
        int64_t cadenceParkedFrames = 0;   // frames produced while anim paused
        double cadenceActiveFps = 0.0;     // estimated FPS when active (reporting rate)

        // Recovery telemetry (from Dart-reported sub-frame measurements)
        double recoveryLastMs = -1.0;      // most recent recovery latency
        double recoveryBestMs = -1.0;      // best across all cycles
        double recoveryWorstMs = -1.0;     // worst across all cycles
        int recoveryResumeCycles = 0;      // total park/resume cycles
    };

    // Test configuration.
    struct Config {
        int engineCount = 2;
        int durationSec = 300;       // 5 min default
        int sampleIntervalMs = 10000; // 10s default
        bool autoParkAfterAttach = true;
        int parkDelayMs = 5000;       // park 5s after attach
        std::string testName = "sustainability";
    };

    // Take a single snapshot right now.
    static Snapshot takeSnapshot(const RendererManager& mgr,
                                 double elapsedSec,
                                 int64_t prevTotalFrames,
                                 const std::string& phase) {
        Snapshot s;
        s.timestampSec = elapsedSec;
        s.phase = phase;

        // Process-level memory
        PROCESS_MEMORY_COUNTERS_EX pmc = {};
        pmc.cb = sizeof(pmc);
        if (GetProcessMemoryInfo(GetCurrentProcess(),
                                  reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                                  sizeof(pmc))) {
            s.privateKB = static_cast<int64_t>(pmc.PrivateUsage / 1024);
            s.workingSetKB = static_cast<int64_t>(pmc.WorkingSetSize / 1024);
        }

        // Process-level resource counts
        HANDLE proc = GetCurrentProcess();
        DWORD handleCount = 0;
        GetProcessHandleCount(proc, &handleCount);
        s.handleCount = static_cast<int32_t>(handleCount);
        s.gdiObjects = static_cast<int32_t>(GetGuiResources(proc, GR_GDIOBJECTS));
        s.userObjects = static_cast<int32_t>(GetGuiResources(proc, GR_USEROBJECTS));

        // Thread count via snapshot (or from existing telemetry)
        // Use the process info approach — lightweight
        s.threadCount = 0;
        HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (hSnap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = {};
            te.dwSize = sizeof(te);
            DWORD pid = GetCurrentProcessId();
            if (Thread32First(hSnap, &te)) {
                do {
                    if (te.th32OwnerProcessID == pid) {
                        s.threadCount++;
                    }
                } while (Thread32Next(hSnap, &te));
            }
            CloseHandle(hSnap);
        }

        // Renderer state aggregation (from RendererManager records)
        int64_t totalFrames = 0;
        for (const auto& [id, rec] : mgr.records()) {
            s.rendererTotal++;
            switch (rec.activity) {
                case ActivityState::Active:    s.rendererActive++; break;
                case ActivityState::Throttled: s.rendererThrottled++; break;
                case ActivityState::Parked:    s.rendererParked++; break;
                case ActivityState::Dormant:   s.rendererDormant++; break;
            }
            totalFrames += rec.totalFramesRendered;
        }

        // Frame cadence (from FrameCadenceMonitor — the TRUE frame counter)
        // This is where Dart engines report their actual frame production.
        auto allCadence = FrameCadenceMonitor::instance().queryAll();
        int64_t cadenceTotal = 0;
        int64_t cadenceHidden = 0;
        int64_t cadenceParked = 0;
        double maxFps = 0.0;
        for (const auto& [engineId, data] : allCadence) {
            cadenceTotal += data.totalFrames;
            cadenceHidden += data.framesWhileHidden;
            cadenceParked += data.framesWhileAnimPaused;
            if (data.estimatedFps > maxFps) maxFps = data.estimatedFps;
        }

        // Use cadence monitor's total if available, fall back to renderer manager's
        s.totalFramesRendered = (cadenceTotal > 0) ? cadenceTotal : totalFrames;
        s.frameDeltaSinceLastSnapshot = s.totalFramesRendered - prevTotalFrames;
        s.cadenceTotalFrames = cadenceTotal;
        s.cadenceHiddenFrames = cadenceHidden;
        s.cadenceParkedFrames = cadenceParked;
        s.cadenceActiveFps = maxFps;

        // Recovery telemetry (aggregate best/worst across all engines)
        double bestRecovery = 1e9;
        double worstRecovery = 0.0;
        double lastRecovery = -1.0;
        int totalCycles = 0;
        for (const auto& [engineId, data] : allCadence) {
            if (data.lastRecoveryMs >= 0.0) {
                lastRecovery = data.lastRecoveryMs;
            }
            if (data.bestRecoveryMs < bestRecovery && data.resumeCycleCount > 0) {
                bestRecovery = data.bestRecoveryMs;
            }
            if (data.worstRecoveryMs > worstRecovery) {
                worstRecovery = data.worstRecoveryMs;
            }
            totalCycles += data.resumeCycleCount;
        }
        s.recoveryLastMs = lastRecovery;
        s.recoveryBestMs = (bestRecovery < 1e9) ? bestRecovery : -1.0;
        s.recoveryWorstMs = (worstRecovery > 0.0) ? worstRecovery : -1.0;
        s.recoveryResumeCycles = totalCycles;

        return s;
    }

    // Format a snapshot as a CSV row.
    static std::string snapshotToCsvRow(const Snapshot& s) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1)
            << s.timestampSec << ","
            << s.privateKB << ","
            << s.workingSetKB << ","
            << s.threadCount << ","
            << s.handleCount << ","
            << s.gdiObjects << ","
            << s.userObjects << ","
            << s.rendererTotal << ","
            << s.rendererActive << ","
            << s.rendererParked << ","
            << s.rendererThrottled << ","
            << s.rendererDormant << ","
            << s.totalFramesRendered << ","
            << s.frameDeltaSinceLastSnapshot << ","
            << s.cadenceTotalFrames << ","
            << s.cadenceHiddenFrames << ","
            << s.cadenceParkedFrames << ","
            << std::setprecision(1) << s.cadenceActiveFps << ","
            << std::setprecision(2) << s.recoveryLastMs << ","
            << s.recoveryBestMs << ","
            << s.recoveryWorstMs << ","
            << s.recoveryResumeCycles << ","
            << s.phase;
        return oss.str();
    }

    // CSV header row.
    static std::string csvHeader() {
        return "timestampSec,privateKB,workingSetKB,threadCount,handleCount,"
               "gdiObjects,userObjects,rendererTotal,rendererActive,"
               "rendererParked,rendererThrottled,rendererDormant,"
               "totalFramesRendered,frameDelta,"
               "cadenceTotalFrames,cadenceHiddenFrames,cadenceParkedFrames,cadenceActiveFps,"
               "recoveryLastMs,recoveryBestMs,recoveryWorstMs,recoveryResumeCycles,"
               "phase";
    }

    // Format all snapshots as complete CSV string.
    static std::string snapshotsToCsv(const std::vector<Snapshot>& snapshots,
                                       const Config& config) {
        std::ostringstream oss;
        oss << "# Morphic Sustainability Benchmark\n"
            << "# Test: " << config.testName << "\n"
            << "# Engines: " << config.engineCount << "\n"
            << "# Duration: " << config.durationSec << "s\n"
            << "# Sample interval: " << config.sampleIntervalMs << "ms\n"
            << "# Auto-park: " << (config.autoParkAfterAttach ? "yes" : "no") << "\n"
            << "#\n"
            << csvHeader() << "\n";
        for (const auto& s : snapshots) {
            oss << snapshotToCsvRow(s) << "\n";
        }
        return oss.str();
    }
};

}  // namespace morphic
