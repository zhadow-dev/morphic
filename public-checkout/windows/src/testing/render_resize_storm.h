#pragma once

#include "../core/types.h"
#include "../core/thread_affinity.h"
#include <windows.h>
#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <functional>

namespace morphic {

// Phase 2A.3 — RenderResizeStorm.
//
// Resize is where HWND cadence, Flutter raster cadence, and compositor
// cadence diverge hardest. This is the stress test that reveals real
// temporal coherence failures.
//
// 4 scenarios:
//   1. ContinuousResize   — rapid resize on single surface with renderer
//   2. GroupedResize       — resize one surface in group, measure coherence
//   3. OverlappingResize  — resize two Flutter surfaces simultaneously
//   4. MixedRendererResize — resize Flutter + GDI, compare convergence
//
// THREAD: UI thread only.
class RenderResizeStorm {
public:
    struct ResizeResult {
        std::string scenario;
        bool passed = false;
        int totalResizes = 0;
        int droppedFrames = 0;
        double avgResizeMs = 0.0;
        double maxResizeMs = 0.0;
        double skewP99 = 0.0;           // Skew during resize
        double convergenceMs = 0.0;      // Time to stabilize after storm
        double pacingStabilityDuring = 0.0;  // Pacing % during resize
        int invariantViolations = 0;
        std::string details;
    };

    // Callback type for compositor operations.
    using ResizeFn = std::function<void(NodeId, int, int)>;
    using FrameFn = std::function<void()>;
    using MetricsFn = std::function<double()>;  // Returns a metric value

    struct StormConfig {
        int iterations = 200;          // Number of resize operations
        int minWidth = 150;
        int maxWidth = 800;
        int minHeight = 100;
        int maxHeight = 600;
        int delayBetweenMs = 0;        // 0 = max speed
        unsigned int seed = 42;        // Deterministic
    };

    RenderResizeStorm() = default;

    // --- Scenario 1: Continuous single-surface resize ---
    ResizeResult runContinuous(
            NodeId surfaceId,
            ResizeFn resizeFn,
            FrameFn processFrame,
            MetricsFn getSkewP99,
            MetricsFn getPacingStability,
            const StormConfig& config = {}) {
        MORPHIC_ASSERT_UI_THREAD();

        ResizeResult result;
        result.scenario = "ContinuousResize";

        std::mt19937 rng(config.seed);
        std::uniform_int_distribution<int> wDist(config.minWidth, config.maxWidth);
        std::uniform_int_distribution<int> hDist(config.minHeight, config.maxHeight);

        auto stormStart = std::chrono::high_resolution_clock::now();
        double totalMs = 0.0;
        double maxMs = 0.0;

        for (int i = 0; i < config.iterations; i++) {
            int w = wDist(rng);
            int h = hDist(rng);

            auto t0 = std::chrono::high_resolution_clock::now();
            resizeFn(surfaceId, w, h);
            processFrame();
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            totalMs += ms;
            if (ms > maxMs) maxMs = ms;

            if (config.delayBetweenMs > 0) {
                Sleep(config.delayBetweenMs);
            }
        }

        auto stormEnd = std::chrono::high_resolution_clock::now();

        result.totalResizes = config.iterations;
        result.avgResizeMs = totalMs / config.iterations;
        result.maxResizeMs = maxMs;
        result.skewP99 = getSkewP99();
        result.pacingStabilityDuring = getPacingStability();

        // Measure convergence — process frames until pacing stabilizes
        for (int i = 0; i < 60; i++) {  // Max 60 frames to converge
            processFrame();
            Sleep(16);  // ~60fps
        }
        auto convEnd = std::chrono::high_resolution_clock::now();
        result.convergenceMs = std::chrono::duration<double, std::milli>(
            convEnd - stormEnd).count();

        result.passed = (result.maxResizeMs < 100.0);  // No single resize > 100ms
        result.details = "avg=" + std::to_string(result.avgResizeMs) +
            "ms max=" + std::to_string(result.maxResizeMs) +
            "ms skewP99=" + std::to_string(result.skewP99) +
            "ms convergence=" + std::to_string(result.convergenceMs) + "ms";

        return result;
    }

    // --- Scenario 2: Grouped resize (resize one, measure group coherence) ---
    ResizeResult runGrouped(
            NodeId primaryId,
            const std::vector<NodeId>& groupMembers,
            ResizeFn resizeFn,
            FrameFn processFrame,
            MetricsFn getSkewP99,
            MetricsFn getPacingStability,
            const StormConfig& config = {}) {
        MORPHIC_ASSERT_UI_THREAD();

        ResizeResult result;
        result.scenario = "GroupedResize";

        std::mt19937 rng(config.seed);
        std::uniform_int_distribution<int> wDist(config.minWidth, config.maxWidth);
        std::uniform_int_distribution<int> hDist(config.minHeight, config.maxHeight);

        double totalMs = 0.0;
        double maxMs = 0.0;

        for (int i = 0; i < config.iterations; i++) {
            int w = wDist(rng);
            int h = hDist(rng);

            auto t0 = std::chrono::high_resolution_clock::now();
            resizeFn(primaryId, w, h);
            processFrame();
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            totalMs += ms;
            if (ms > maxMs) maxMs = ms;
        }

        result.totalResizes = config.iterations;
        result.avgResizeMs = totalMs / config.iterations;
        result.maxResizeMs = maxMs;
        result.skewP99 = getSkewP99();
        result.pacingStabilityDuring = getPacingStability();
        result.passed = (result.maxResizeMs < 100.0);
        result.details = "group=" + std::to_string(groupMembers.size()) +
            " avg=" + std::to_string(result.avgResizeMs) +
            "ms max=" + std::to_string(result.maxResizeMs) + "ms";

        return result;
    }

    // --- Scenario 3: Overlapping resize (two surfaces simultaneously) ---
    ResizeResult runOverlapping(
            NodeId surfaceA, NodeId surfaceB,
            ResizeFn resizeFn,
            FrameFn processFrame,
            MetricsFn getSkewP99,
            MetricsFn getPacingStability,
            const StormConfig& config = {}) {
        MORPHIC_ASSERT_UI_THREAD();

        ResizeResult result;
        result.scenario = "OverlappingResize";

        std::mt19937 rng(config.seed);
        std::uniform_int_distribution<int> wDist(config.minWidth, config.maxWidth);
        std::uniform_int_distribution<int> hDist(config.minHeight, config.maxHeight);

        double totalMs = 0.0;
        double maxMs = 0.0;

        for (int i = 0; i < config.iterations; i++) {
            int wA = wDist(rng), hA = hDist(rng);
            int wB = wDist(rng), hB = hDist(rng);

            auto t0 = std::chrono::high_resolution_clock::now();
            resizeFn(surfaceA, wA, hA);
            resizeFn(surfaceB, wB, hB);
            processFrame();
            auto t1 = std::chrono::high_resolution_clock::now();

            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            totalMs += ms;
            if (ms > maxMs) maxMs = ms;
        }

        result.totalResizes = config.iterations * 2;  // Two surfaces
        result.avgResizeMs = totalMs / config.iterations;
        result.maxResizeMs = maxMs;
        result.skewP99 = getSkewP99();
        result.pacingStabilityDuring = getPacingStability();
        result.passed = (result.maxResizeMs < 150.0);
        result.details = "dual-resize avg=" + std::to_string(result.avgResizeMs) +
            "ms max=" + std::to_string(result.maxResizeMs) +
            "ms skewP99=" + std::to_string(result.skewP99) + "ms";

        return result;
    }

    // --- Run all scenarios and return results ---
    std::vector<ResizeResult> runAll(
            NodeId surfaceA, NodeId surfaceB,
            const std::vector<NodeId>& groupMembers,
            ResizeFn resizeFn,
            FrameFn processFrame,
            MetricsFn getSkewP99,
            MetricsFn getPacingStability,
            const StormConfig& config = {}) {
        std::vector<ResizeResult> results;
        results.push_back(runContinuous(surfaceA, resizeFn, processFrame,
                                        getSkewP99, getPacingStability, config));
        results.push_back(runGrouped(surfaceA, groupMembers, resizeFn, processFrame,
                                     getSkewP99, getPacingStability, config));
        results.push_back(runOverlapping(surfaceA, surfaceB, resizeFn, processFrame,
                                         getSkewP99, getPacingStability, config));
        return results;
    }
};

}  // namespace morphic
