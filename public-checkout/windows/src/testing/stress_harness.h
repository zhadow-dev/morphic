#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"
#include "../debug/metrics_collector.h"
#include "invariant_checker.h"
#include "ordering_validator.h"
#include "drift_detector.h"

#include <windows.h>
#include <vector>
#include <string>
#include <functional>
#include <random>
#include <sstream>
#include <chrono>
#include <unordered_map>
#include <memory>

namespace morphic {

class Compositor;

// Phase 1B Track 6 — Automated stress harness.
//
// Torture tests that run inside the compositor process.
// Each test produces a deterministic pass/fail result.
// NO manual clicking required.
class StressHarness {
public:
    struct TestResult {
        std::string testName;
        int iterations = 0;
        double durationMs = 0.0;
        double avgFrameMs = 0.0;
        double maxFrameMs = 0.0;
        int droppedFrames = 0;
        double maxSyncError = 0.0;
        int invariantViolations = 0;
        int orderingMismatches = 0;
        int orphanHwnds = 0;
        bool passed = false;
        std::string failureReason;
    };

    // Callbacks for compositor interaction (avoids circular dependency)
    struct CompositorCallbacks {
        std::function<void(NodeId, int, int)> moveSurface;
        std::function<void(NodeId, int, int)> resizeSurface;
        std::function<void(NodeId, POINT)> dragBegin;
        std::function<void(NodeId, POINT)> dragUpdate;
        std::function<void(NodeId, POINT)> dragEnd;
        std::function<void(NodeId)> activate;
        std::function<void()> processFrame;
        std::function<std::vector<NodeId>()> getSurfaceIds;
        std::function<const SceneGraph&()> getSceneGraph;
        std::function<const std::unordered_map<NodeId, std::unique_ptr<WindowHost>>&()> getHosts;
        std::function<const MetricsCollector&()> getMetrics;
    };

    // --- Test A1: Drag Marathon ---
    // Continuous drag for N milliseconds. Verifies 0 drops, sync < 1px.
    TestResult runDragMarathon(CompositorCallbacks& cb, int durationMs = 5000) {
        TestResult result;
        result.testName = "DragMarathon";
        auto start = std::chrono::steady_clock::now();

        auto ids = cb.getSurfaceIds();
        if (ids.empty()) { result.failureReason = "No surfaces"; return result; }

        NodeId dragId = ids[0];
        POINT startPt = {500, 300};
        cb.dragBegin(dragId, startPt);

        int frames = 0;
        double maxFrame = 0;
        int dropped = 0;

        while (true) {
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > durationMs) break;

            // Sinusoidal drag pattern to cover screen
            double t = elapsed / 1000.0;
            int dx = static_cast<int>(300 * sin(t * 2.0));
            int dy = static_cast<int>(200 * cos(t * 1.5));
            POINT pt = {startPt.x + dx, startPt.y + dy};
            cb.dragUpdate(dragId, pt);
            cb.processFrame();

            auto& metrics = cb.getMetrics();
            auto& last = metrics.lastFrame();
            if (last.frameTimeMs > maxFrame) maxFrame = last.frameTimeMs;
            if (last.dropped) dropped++;
            frames++;
        }

        cb.dragEnd(dragId, startPt);
        cb.processFrame();

        auto& metrics = cb.getMetrics();
        result.iterations = frames;
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        result.avgFrameMs = result.durationMs / frames;
        result.maxFrameMs = maxFrame;
        result.droppedFrames = dropped;
        result.maxSyncError = metrics.syncMetrics().maxPositionalDivergence;
        result.passed = (dropped == 0 && result.maxSyncError < 1.0);
        if (!result.passed) {
            result.failureReason = "drops=" + std::to_string(dropped) +
                " syncErr=" + std::to_string(result.maxSyncError);
        }
        return result;
    }

    // --- Test A2: Resize Spam ---
    // 1000 rapid resizes. Verifies 0 transform mismatches.
    TestResult runResizeSpam(CompositorCallbacks& cb, int iterations = 1000) {
        TestResult result;
        result.testName = "ResizeSpam";
        auto start = std::chrono::steady_clock::now();

        auto ids = cb.getSurfaceIds();
        if (ids.empty()) { result.failureReason = "No surfaces"; return result; }

        std::mt19937 rng(42);  // Deterministic seed
        std::uniform_int_distribution<int> sizeDist(150, 800);  // >= minWidth/Height

        int mismatches = 0;
        for (int i = 0; i < iterations; i++) {
            NodeId id = ids[i % ids.size()];
            int w = sizeDist(rng);
            int h = sizeDist(rng);
            cb.resizeSurface(id, w, h);
            cb.processFrame();

            // Verify scene graph matches HWND
            auto& graph = cb.getSceneGraph();
            auto& hosts = cb.getHosts();
            auto* surface = const_cast<SceneGraph&>(graph).getSurface(id);
            auto it = hosts.find(id);
            if (surface && it != hosts.end() && it->second && it->second->isAlive()) {
                RECT wr;
                GetWindowRect(it->second->hwnd(), &wr);
                int actualW = wr.right - wr.left;
                int actualH = wr.bottom - wr.top;
                auto& wt = surface->worldTransform();
                if (std::abs(wt.width - actualW) > 1 || std::abs(wt.height - actualH) > 1) {
                    mismatches++;
                }
            }
        }

        result.iterations = iterations;
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        result.maxSyncError = static_cast<double>(mismatches);
        result.passed = (mismatches == 0);
        if (!result.passed) {
            result.failureReason = "transform mismatches=" + std::to_string(mismatches);
        }
        return result;
    }

    // --- Test A3: Elevation Churn ---
    // 500 rapid activation switches. Verifies 0 z-order mismatches.
    TestResult runElevationChurn(CompositorCallbacks& cb, int iterations = 500) {
        TestResult result;
        result.testName = "ElevationChurn";
        auto start = std::chrono::steady_clock::now();

        auto ids = cb.getSurfaceIds();
        if (ids.size() < 2) { result.failureReason = "Need 2+ surfaces"; return result; }

        OrderingValidator validator;
        int mismatches = 0;

        for (int i = 0; i < iterations; i++) {
            NodeId id = ids[i % ids.size()];
            cb.activate(id);
            cb.processFrame();

            auto orderResult = validator.validate(cb.getSceneGraph(), cb.getHosts());
            if (!orderResult.consistent) {
                mismatches++;
            }
        }

        result.iterations = iterations;
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        result.orderingMismatches = mismatches;
        result.passed = (mismatches == 0);
        if (!result.passed) {
            result.failureReason = "z-order mismatches=" + std::to_string(mismatches);
        }
        return result;
    }

    // --- Test A5: Transaction Flood ---
    // 2000 rapid multi-op transactions. Verifies 0 partial commits.
    TestResult runTransactionFlood(CompositorCallbacks& cb, int iterations = 2000) {
        TestResult result;
        result.testName = "TransactionFlood";
        auto start = std::chrono::steady_clock::now();

        auto ids = cb.getSurfaceIds();
        if (ids.empty()) { result.failureReason = "No surfaces"; return result; }

        std::mt19937 rng(42);
        std::uniform_int_distribution<int> posDist(100, 1200);
        InvariantChecker checker;
        int violations = 0;

        for (int i = 0; i < iterations; i++) {
            // Move all surfaces to random positions
            for (auto id : ids) {
                cb.moveSurface(id, posDist(rng), posDist(rng));
            }
            cb.processFrame();

            // Check invariants hold
            auto inv = checker.validateAll(cb.getSceneGraph(), cb.getHosts(), i);
            violations += static_cast<int>(inv.size());
        }

        result.iterations = iterations;
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        result.invariantViolations = violations;
        result.passed = (violations == 0);
        if (!result.passed) {
            result.failureReason = "invariant violations=" + std::to_string(violations);
        }
        return result;
    }

    // --- Test A6: Mutation Storm ---
    // Rapid mixed operations for N seconds.
    TestResult runMutationStorm(CompositorCallbacks& cb, int durationMs = 5000) {
        TestResult result;
        result.testName = "MutationStorm";
        auto start = std::chrono::steady_clock::now();

        auto ids = cb.getSurfaceIds();
        if (ids.size() < 2) { result.failureReason = "Need 2+ surfaces"; return result; }

        std::mt19937 rng(42);
        std::uniform_int_distribution<int> actionDist(0, 3);
        std::uniform_int_distribution<int> posDist(100, 1200);
        std::uniform_int_distribution<int> sizeDist(150, 800);  // >= minWidth/Height

        InvariantChecker checker;
        int frames = 0;
        int violations = 0;

        while (true) {
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed > durationMs) break;

            NodeId id = ids[rng() % ids.size()];
            int action = actionDist(rng);

            switch (action) {
                case 0: cb.moveSurface(id, posDist(rng), posDist(rng)); break;
                case 1: cb.resizeSurface(id, sizeDist(rng), sizeDist(rng)); break;
                case 2: cb.activate(id); break;
                case 3: cb.activate(ids[rng() % ids.size()]); break;
            }

            cb.processFrame();
            auto inv = checker.validateAll(cb.getSceneGraph(), cb.getHosts(), frames);
            violations += static_cast<int>(inv.size());
            frames++;
        }

        result.iterations = frames;
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        result.invariantViolations = violations;
        result.maxSyncError = cb.getMetrics().syncMetrics().maxPositionalDivergence;
        result.passed = (violations == 0 && result.maxSyncError < 1.0);
        if (!result.passed) {
            result.failureReason = "violations=" + std::to_string(violations) +
                " syncErr=" + std::to_string(result.maxSyncError);
        }
        return result;
    }

    // --- Test A9: Focus Churn ---
    // Rapid activation switching simulating alt-tab storms.
    TestResult runFocusChurn(CompositorCallbacks& cb, int iterations = 1000) {
        TestResult result;
        result.testName = "FocusChurn";
        auto start = std::chrono::steady_clock::now();

        auto ids = cb.getSurfaceIds();
        if (ids.size() < 2) { result.failureReason = "Need 2+ surfaces"; return result; }

        OrderingValidator validator;
        int mismatches = 0;

        // Rapid back-and-forth activation
        for (int i = 0; i < iterations; i++) {
            // Activate each surface in rapid succession
            for (auto id : ids) {
                cb.activate(id);
            }
            cb.processFrame();

            auto orderResult = validator.validate(cb.getSceneGraph(), cb.getHosts());
            if (!orderResult.consistent) {
                mismatches++;
            }
        }

        result.iterations = iterations;
        result.durationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        result.orderingMismatches = mismatches;
        result.passed = (mismatches == 0);
        if (!result.passed) {
            result.failureReason = "focus ordering mismatches=" + std::to_string(mismatches);
        }
        return result;
    }

    // --- Run all tests ---
    std::vector<TestResult> runAll(CompositorCallbacks& cb) {
        std::vector<TestResult> results;
        results.push_back(runDragMarathon(cb, 5000));
        results.push_back(runResizeSpam(cb, 1000));
        results.push_back(runElevationChurn(cb, 500));
        results.push_back(runTransactionFlood(cb, 2000));
        results.push_back(runMutationStorm(cb, 5000));
        results.push_back(runFocusChurn(cb, 1000));
        return results;
    }

    // Format results as string
    static std::string formatResults(const std::vector<TestResult>& results) {
        std::ostringstream oss;
        oss << "=== STRESS HARNESS RESULTS ===\n";
        int passed = 0, failed = 0;
        for (const auto& r : results) {
            oss << (r.passed ? "[PASS] " : "[FAIL] ")
                << r.testName << " — "
                << r.iterations << " iters, "
                << r.durationMs << "ms";
            if (!r.passed) {
                oss << " — " << r.failureReason;
                failed++;
            } else {
                passed++;
            }
            oss << "\n";
        }
        oss << "=== " << passed << " passed, " << failed << " failed ===\n";
        return oss.str();
    }
};

}  // namespace morphic
