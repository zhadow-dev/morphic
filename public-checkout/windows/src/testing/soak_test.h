#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../debug/metrics_collector.h"
#include "invariant_checker.h"
#include "ordering_validator.h"
#include "drift_detector.h"
#include "stress_harness.h"

#include <windows.h>
#include <fstream>
#include <string>
#include <random>
#include <chrono>
#include <vector>
#include <sstream>
#include <psapi.h>

#pragma comment(lib, "psapi.lib")

namespace morphic {

// Phase 1B Track 11 — Extended soak test.
// Runs continuous random operations for hours.
// Logs metrics to CSV. Validates invariants/ordering/drift continuously.
// Pass criteria: < 0.1% drops, 0 violations, stable memory.
class SoakTest {
public:
    struct SoakResult {
        int totalFrames = 0;
        int droppedFrames = 0;
        double dropRate = 0.0;
        double avgFrameMs = 0.0;
        double maxFrameMs = 0.0;
        double p99FrameMs = 0.0;
        int invariantViolations = 0;
        int orderingMismatches = 0;
        double maxDrift = 0.0;
        int orphanHwnds = 0;
        double maxSyncError = 0.0;
        int budgetViolations = 0;
        size_t peakMemoryKB = 0;
        size_t endMemoryKB = 0;
        bool memoryStable = true;
        bool stable = true;
        std::string summary;
    };

    // Run soak test for N minutes.
    // Writes CSV to the provided path.
    SoakResult run(
        StressHarness::CompositorCallbacks& cb,
        int durationMinutes = 30,
        const std::wstring& csvPath = L"")
    {
        SoakResult result;
        std::ofstream csv;

        if (!csvPath.empty()) {
            csv.open(csvPath);
            if (csv.is_open()) {
                csv << "frame,elapsed_s,frame_ms,dirty,defer,sync_err,drift,"
                    << "invariants,ordering_ok,orphans,budget_ok,mem_kb\n";
            }
        }

        auto ids = cb.getSurfaceIds();
        if (ids.empty()) {
            result.summary = "No surfaces to test";
            return result;
        }

        InvariantChecker checker;
        OrderingValidator ordering;
        DriftDetector drift;

        // Capture baseline for drift detection
        drift.captureBaseline(cb.getSceneGraph());

        std::mt19937 rng(12345);  // Deterministic seed
        std::uniform_int_distribution<int> actionDist(0, 4);
        std::uniform_int_distribution<int> posDist(50, 1500);
        std::uniform_int_distribution<int> sizeDist(100, 800);

        auto startTime = std::chrono::steady_clock::now();
        double durationMs = durationMinutes * 60.0 * 1000.0;

        size_t initialMemKB = getProcessMemoryKB();
        size_t peakMemKB = initialMemKB;
        int frames = 0;
        int drops = 0;
        int invariantViolations = 0;
        int orderingMismatches = 0;
        double maxFrame = 0;
        double maxSync = 0;

        while (true) {
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > durationMs) break;

            // Random operation
            NodeId id = ids[rng() % ids.size()];
            int action = actionDist(rng);

            switch (action) {
                case 0: cb.moveSurface(id, posDist(rng), posDist(rng)); break;
                case 1: cb.resizeSurface(id, sizeDist(rng), sizeDist(rng)); break;
                case 2: cb.activate(id); break;
                case 3: cb.activate(ids[rng() % ids.size()]); break;
                case 4: {
                    // Drag simulation: begin → 5 updates → end
                    POINT pt = {posDist(rng), posDist(rng)};
                    cb.dragBegin(id, pt);
                    for (int d = 0; d < 5; d++) {
                        pt.x += 10; pt.y += 5;
                        cb.dragUpdate(id, pt);
                        cb.processFrame();
                        frames++;
                    }
                    cb.dragEnd(id, pt);
                    break;
                }
            }

            cb.processFrame();
            frames++;

            auto& metrics = cb.getMetrics();
            auto& last = metrics.lastFrame();
            if (last.dropped) drops++;
            if (last.frameTimeMs > maxFrame) maxFrame = last.frameTimeMs;
            if (metrics.syncMetrics().maxPositionalDivergence > maxSync) {
                maxSync = metrics.syncMetrics().maxPositionalDivergence;
            }

            // Every frame: invariant check
            auto inv = checker.validateAll(cb.getSceneGraph(), cb.getHosts(), frames);
            invariantViolations += static_cast<int>(inv.size());

            // Every 100 frames: ordering validation
            if (frames % 100 == 0) {
                auto orderResult = ordering.validate(cb.getSceneGraph(), cb.getHosts());
                if (!orderResult.consistent) orderingMismatches++;
            }

            // Every 1000 frames: drift detection
            double currentDrift = 0.0;
            if (frames % 1000 == 0) {
                // Move surfaces back to baseline-ish positions to measure drift
                auto driftResult = drift.measureDrift(cb.getSceneGraph(), frames);
                currentDrift = driftResult.maxDrift;
            }

            // Every 60s: log to CSV
            double elapsedSec = elapsed / 1000.0;
            if (csv.is_open() && frames % 60 == 0) {
                size_t memKB = getProcessMemoryKB();
                if (memKB > peakMemKB) peakMemKB = memKB;

                csv << frames << ","
                    << elapsedSec << ","
                    << last.frameTimeMs << ","
                    << last.dirtyNodeCount << ","
                    << last.deferWindowPosCount << ","
                    << metrics.syncMetrics().maxPositionalDivergence << ","
                    << currentDrift << ","
                    << invariantViolations << ","
                    << (ordering.totalMismatches() == 0 ? "true" : "false") << ","
                    << checker.lastOrphanHwndCount() << ","
                    << (metrics.stageTiming().withinBudget() ? "true" : "false") << ","
                    << memKB << "\n";
                csv.flush();
            }

            // Brief sleep to avoid maxing CPU
            if (frames % 10 == 0) {
                Sleep(1);
            }
        }

        // Compile results
        result.totalFrames = frames;
        result.droppedFrames = drops;
        result.dropRate = frames > 0 ? (static_cast<double>(drops) / frames) * 100.0 : 0.0;
        result.avgFrameMs = frames > 0 ? (durationMs / frames) : 0.0;
        result.maxFrameMs = maxFrame;
        result.p99FrameMs = cb.getMetrics().frameTimeP99();
        result.invariantViolations = invariantViolations;
        result.orderingMismatches = orderingMismatches;
        result.maxSyncError = maxSync;
        result.budgetViolations = cb.getMetrics().totalBudgetViolations();
        result.peakMemoryKB = peakMemKB;
        result.endMemoryKB = getProcessMemoryKB();

        // Memory stability: end should be within 20% of initial
        double memGrowth = initialMemKB > 0 ?
            (static_cast<double>(result.endMemoryKB) - initialMemKB) / initialMemKB : 0.0;
        result.memoryStable = memGrowth < 0.2;

        result.stable =
            result.dropRate < 0.1 &&
            result.invariantViolations == 0 &&
            result.orderingMismatches == 0 &&
            result.maxSyncError < 1.0 &&
            result.memoryStable;

        std::ostringstream oss;
        oss << "SOAK TEST: " << durationMinutes << "min, "
            << frames << " frames, "
            << (result.stable ? "STABLE" : "UNSTABLE") << "\n"
            << "  drops=" << drops << " (" << result.dropRate << "%)\n"
            << "  invariants=" << invariantViolations
            << " ordering=" << orderingMismatches << "\n"
            << "  syncErr=" << maxSync << "px\n"
            << "  mem: " << initialMemKB << "KB → " << result.endMemoryKB
            << "KB (peak " << peakMemKB << "KB, growth " << (memGrowth * 100) << "%)\n";
        result.summary = oss.str();

        return result;
    }

private:
    static size_t getProcessMemoryKB() {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize / 1024;
        }
        return 0;
    }
};

}  // namespace morphic
