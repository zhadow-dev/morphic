#pragma once

#include "../core/types.h"
#include "../core/scene_graph.h"
#include "../core/window_host.h"
#include "../core/runtime_events.h"
#include "../core/interaction_phase.h"
#include "../debug/metrics_collector.h"
#include "invariant_checker.h"
#include "ordering_validator.h"
#include "drift_detector.h"
#include "temporal_auditor.h"
#include "performance_budget.h"
#include "stress_harness.h"

#include <windows.h>
#include <psapi.h>
#include <vector>
#include <string>
#include <functional>
#include <random>
#include <sstream>
#include <fstream>
#include <chrono>
#include <unordered_map>
#include <memory>

#pragma comment(lib, "psapi.lib")

namespace morphic {

// Phase 8B.6 — Extended Soak Harness.
//
// Hours-long orchestration survival testing.
// Builds on existing SoakTest and StressHarness but adds:
//
//   - Capture theft injection
//   - Group/ungroup churn
//   - Destroy during active interaction
//   - DPI churn hooks
//   - Activation churn
//   - Randomized timing perturbation
//
// Must support 1000+ interaction sessions.
// Continuous invariant + temporal auditing throughout.
// Automatic JSON report generation.
//
// OBSERVATIONAL ONLY in its auditing. The stress operations
// exercise the runtime through its public API — they do not
// bypass orchestration or directly mutate model state.
class SoakHarness {
public:
    // Extended callbacks (builds on StressHarness::CompositorCallbacks)
    struct SoakCallbacks : StressHarness::CompositorCallbacks {
        // Chaos operations
        std::function<void(NodeId)> destroySurface;
        std::function<NodeId(const std::vector<NodeId>&)> createGroup;
        std::function<void(NodeId)> destroyGroup;
        std::function<NodeId(const SurfaceConfig&)> createSurface;

        // Capture operations
        std::function<void()> releaseCapture;            // Simulate external capture steal
        std::function<bool(NodeId)> isCaptured;

        // EventBus observation
        std::function<size_t()> getTotalSubscriberCount;
        std::function<size_t()> getPendingDeferredCount;
        std::function<size_t()> getPendingTransactionalCount;

        // Capture manager observation
        std::function<bool()> hasCaptureOwner;
    };

    struct MemorySnapshot {
        double elapsedSeconds = 0.0;
        size_t workingSetKB = 0;
        size_t peakWorkingSetKB = 0;
    };

    struct SoakSummary {
        // Core counts
        int totalSessions = 0;
        int totalFrames = 0;
        double totalDurationMs = 0.0;

        // Invariant health
        int invariantFailures = 0;
        int topologyInconsistencies = 0;

        // Temporal health
        double peakTickMs = 0.0;
        double avgTickMs = 0.0;
        double p99TickMs = 0.0;
        int temporalWarns = 0;
        int temporalAlerts = 0;
        int temporalFails = 0;
        int burstStalls = 0;

        // Leak detection
        int orphanedSessions = 0;
        int transactionLeaks = 0;
        int captureLeakCount = 0;
        int subscriberLeaks = 0;

        // Memory
        size_t startMemoryKB = 0;
        size_t endMemoryKB = 0;
        size_t peakMemoryKB = 0;
        bool memoryStable = true;
        std::vector<MemorySnapshot> memoryTrend;

        // Budget
        int budgetViolations = 0;

        // Classification
        bool stable = true;
        std::string failureReason;
    };

    // --- Run soak for N minutes ---
    SoakSummary run(
        SoakCallbacks& cb,
        int durationMinutes = 30,
        int targetSessionCount = 1000)
    {
        SoakSummary summary;

        auto ids = cb.getSurfaceIds();
        if (ids.empty()) {
            summary.failureReason = "No surfaces available";
            summary.stable = false;
            return summary;
        }

        // Initialize subsystems
        InvariantChecker checker;
        OrderingValidator ordering;
        TemporalAuditor temporal;
        PerformanceBudgetMonitor budget;
        temporal.setTargetTickMs(16.67);

        std::mt19937 rng(98765);  // Deterministic seed
        std::uniform_int_distribution<int> actionDist(0, 9);
        std::uniform_int_distribution<int> posDist(50, 1500);
        std::uniform_int_distribution<int> sizeDist(150, 800);
        std::uniform_int_distribution<int> jitterDist(0, 5);

        auto startTime = std::chrono::steady_clock::now();
        double durationMs = durationMinutes * 60.0 * 1000.0;

        summary.startMemoryKB = getProcessMemoryKB();
        size_t peakMem = summary.startMemoryKB;
        size_t initialSubscribers = cb.getTotalSubscriberCount ? cb.getTotalSubscriberCount() : 0;

        int frames = 0;
        int sessions = 0;
        NodeId currentGroupId = kInvalidNodeId;
        bool inDrag = false;
        NodeId dragSurface = kInvalidNodeId;

        while (true) {
            auto elapsed = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - startTime).count();
            if (elapsed > durationMs) break;

            // Refresh surface IDs periodically (surfaces may be created/destroyed)
            if (frames % 100 == 0) {
                ids = cb.getSurfaceIds();
                if (ids.empty()) break;
            }

            // QPC for tick measurement
            LARGE_INTEGER tickStart, tickEnd;
            QueryPerformanceCounter(&tickStart);

            NodeId id = ids[rng() % ids.size()];
            int action = actionDist(rng);
            InteractionPhase currentPhase = InteractionPhase::Idle;

            switch (action) {
                case 0: // Move
                    cb.moveSurface(id, posDist(rng), posDist(rng));
                    currentPhase = InteractionPhase::Drag;
                    break;

                case 1: // Resize
                    cb.resizeSurface(id, sizeDist(rng), sizeDist(rng));
                    currentPhase = InteractionPhase::Resize;
                    break;

                case 2: // Activate
                    cb.activate(id);
                    currentPhase = InteractionPhase::Activation;
                    break;

                case 3: { // Drag simulation (5 updates)
                    POINT pt = {static_cast<LONG>(posDist(rng)), static_cast<LONG>(posDist(rng))};
                    cb.dragBegin(id, pt);
                    inDrag = true;
                    dragSurface = id;
                    currentPhase = InteractionPhase::Drag;
                    for (int d = 0; d < 5; d++) {
                        pt.x += 10; pt.y += 5;
                        cb.dragUpdate(id, pt);
                        cb.processFrame();
                        frames++;
                    }
                    cb.dragEnd(id, pt);
                    inDrag = false;
                    sessions++;
                    break;
                }

                case 4: // Capture theft
                    if (cb.releaseCapture) {
                        cb.releaseCapture();
                        currentPhase = InteractionPhase::CaptureLoss;
                    }
                    break;

                case 5: // Group/ungroup churn
                    if (ids.size() >= 2 && cb.createGroup && cb.destroyGroup) {
                        if (currentGroupId == kInvalidNodeId) {
                            std::vector<NodeId> members = {ids[0], ids[1]};
                            currentGroupId = cb.createGroup(members);
                            currentPhase = InteractionPhase::GroupedDrag;
                        } else {
                            cb.destroyGroup(currentGroupId);
                            currentGroupId = kInvalidNodeId;
                        }
                    }
                    break;

                case 6: // Activation churn (rapid cycling)
                    currentPhase = InteractionPhase::Activation;
                    for (size_t i = 0; i < ids.size() && i < 5; i++) {
                        cb.activate(ids[i]);
                        cb.processFrame();
                        frames++;
                    }
                    break;

                case 7: // Timing perturbation
                    if (jitterDist(rng) > 3) {
                        Sleep(static_cast<DWORD>(jitterDist(rng)));
                    }
                    break;

                case 8: // Destroy during interaction (if safe)
                    if (ids.size() > 2 && cb.destroySurface && cb.createSurface) {
                        currentPhase = InteractionPhase::Destroy;
                        NodeId victimId = ids.back();
                        // Only destroy if not in active drag
                        if (victimId != dragSurface || !inDrag) {
                            cb.destroySurface(victimId);
                            // Recreate to maintain pool
                            SurfaceConfig config;
                            config.x = posDist(rng);
                            config.y = posDist(rng);
                            config.width = sizeDist(rng);
                            config.height = sizeDist(rng);
                            config.visible = true;
                            cb.createSurface(config);
                        }
                    }
                    break;

                case 9: // Mixed rapid operations
                    for (int i = 0; i < 3; i++) {
                        NodeId mixId = ids[rng() % ids.size()];
                        cb.moveSurface(mixId, posDist(rng), posDist(rng));
                    }
                    break;
            }

            cb.processFrame();
            frames++;

            QueryPerformanceCounter(&tickEnd);
            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);
            double tickMs = (tickEnd.QuadPart - tickStart.QuadPart) * 1000.0 /
                            static_cast<double>(freq.QuadPart);

            // Feed temporal auditor
            temporal.recordTick(tickMs, currentPhase);

            // Feed performance budget (with available metrics)
            auto& metrics = cb.getMetrics();
            size_t subscriberCount = cb.getTotalSubscriberCount ? cb.getTotalSubscriberCount() : 0;
            size_t pendingDeferred = cb.getPendingDeferredCount ? cb.getPendingDeferredCount() : 0;
            budget.evaluateTick(
                frames, currentPhase,
                0,  // projections (not directly available from MetricsCollector)
                0,  // dropped
                0,  // totalEnqueued
                0,  // coalesced
                0,  // maxListeners
                subscriberCount,
                tickMs,
                0.0  // auditCost
            );

            // Invariant check every frame
            auto inv = checker.validateAll(cb.getSceneGraph(), cb.getHosts(), frames);
            summary.invariantFailures += static_cast<int>(inv.size());

            // Ordering check every 100 frames
            if (frames % 100 == 0) {
                auto orderResult = ordering.validate(cb.getSceneGraph(), cb.getHosts());
                if (!orderResult.consistent) summary.topologyInconsistencies++;
            }

            // Memory snapshot every 30 seconds
            double elapsedSec = elapsed / 1000.0;
            if (frames % 1800 == 0) {  // ~30s at 60fps
                size_t memKB = getProcessMemoryKB();
                if (memKB > peakMem) peakMem = memKB;
                summary.memoryTrend.push_back({elapsedSec, memKB, peakMem});
            }

            // Leak detection every 500 frames
            if (frames % 500 == 0) {
                // Transaction leaks
                size_t pending = (cb.getPendingDeferredCount ? cb.getPendingDeferredCount() : 0) +
                                (cb.getPendingTransactionalCount ? cb.getPendingTransactionalCount() : 0);
                if (pending > 10) summary.transactionLeaks++;

                // Capture leaks
                if (cb.hasCaptureOwner && cb.hasCaptureOwner() && !inDrag) {
                    summary.captureLeakCount++;
                }
            }

            // Yield occasionally to avoid maxing CPU
            if (frames % 10 == 0) {
                Sleep(1);
            }

            sessions++;
        }

        // Final measurements
        auto temporalReport = temporal.report();
        summary.totalSessions = sessions;
        summary.totalFrames = frames;
        summary.totalDurationMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - startTime).count();

        summary.peakTickMs = temporalReport.peakTickMs;
        summary.avgTickMs = temporalReport.avgTickMs;
        summary.temporalWarns = temporalReport.warnCount;
        summary.temporalAlerts = temporalReport.alertCount;
        summary.temporalFails = temporalReport.failCount;
        summary.burstStalls = temporalReport.burstStallCount;

        summary.endMemoryKB = getProcessMemoryKB();
        summary.peakMemoryKB = peakMem;

        // Subscriber leak check
        size_t finalSubscribers = cb.getTotalSubscriberCount ? cb.getTotalSubscriberCount() : 0;
        if (finalSubscribers > initialSubscribers + 5) {
            summary.subscriberLeaks = static_cast<int>(finalSubscribers - initialSubscribers);
        }

        // Memory stability: end should be within 25% of start
        if (summary.startMemoryKB > 0) {
            double growth = static_cast<double>(summary.endMemoryKB - summary.startMemoryKB)
                          / static_cast<double>(summary.startMemoryKB);
            summary.memoryStable = growth < 0.25;
        }

        summary.budgetViolations = budget.currentReport().totalViolations();

        // Stability assessment
        summary.stable =
            summary.invariantFailures == 0 &&
            summary.topologyInconsistencies == 0 &&
            summary.transactionLeaks == 0 &&
            summary.captureLeakCount == 0 &&
            summary.subscriberLeaks == 0 &&
            summary.memoryStable;

        if (!summary.stable) {
            std::ostringstream oss;
            if (summary.invariantFailures > 0) oss << "invariant_failures=" << summary.invariantFailures << " ";
            if (summary.topologyInconsistencies > 0) oss << "topology=" << summary.topologyInconsistencies << " ";
            if (summary.transactionLeaks > 0) oss << "tx_leaks=" << summary.transactionLeaks << " ";
            if (summary.captureLeakCount > 0) oss << "capture_leaks=" << summary.captureLeakCount << " ";
            if (summary.subscriberLeaks > 0) oss << "sub_leaks=" << summary.subscriberLeaks << " ";
            if (!summary.memoryStable) oss << "mem_unstable ";
            summary.failureReason = oss.str();
        }

        return summary;
    }

    // --- Serialize summary to JSON ---
    static bool writeSummary(const SoakSummary& s, const std::string& path) {
        std::ofstream out(path);
        if (!out.is_open()) return false;

        out << "{\n";
        out << "  \"total_sessions\": " << s.totalSessions << ",\n";
        out << "  \"total_frames\": " << s.totalFrames << ",\n";
        out << "  \"duration_ms\": " << s.totalDurationMs << ",\n";
        out << "  \"stable\": " << (s.stable ? "true" : "false") << ",\n";
        if (!s.failureReason.empty()) {
            out << "  \"failure_reason\": \"" << s.failureReason << "\",\n";
        }

        out << "  \"invariant_failures\": " << s.invariantFailures << ",\n";
        out << "  \"topology_inconsistencies\": " << s.topologyInconsistencies << ",\n";

        out << "  \"temporal\": {\n";
        out << "    \"peak_tick_ms\": " << s.peakTickMs << ",\n";
        out << "    \"avg_tick_ms\": " << s.avgTickMs << ",\n";
        out << "    \"warn_count\": " << s.temporalWarns << ",\n";
        out << "    \"alert_count\": " << s.temporalAlerts << ",\n";
        out << "    \"fail_count\": " << s.temporalFails << ",\n";
        out << "    \"burst_stalls\": " << s.burstStalls << "\n";
        out << "  },\n";

        out << "  \"leaks\": {\n";
        out << "    \"orphaned_sessions\": " << s.orphanedSessions << ",\n";
        out << "    \"transaction_leaks\": " << s.transactionLeaks << ",\n";
        out << "    \"capture_leaks\": " << s.captureLeakCount << ",\n";
        out << "    \"subscriber_leaks\": " << s.subscriberLeaks << "\n";
        out << "  },\n";

        out << "  \"memory\": {\n";
        out << "    \"start_kb\": " << s.startMemoryKB << ",\n";
        out << "    \"end_kb\": " << s.endMemoryKB << ",\n";
        out << "    \"peak_kb\": " << s.peakMemoryKB << ",\n";
        out << "    \"stable\": " << (s.memoryStable ? "true" : "false") << ",\n";
        out << "    \"trend\": [";
        for (size_t i = 0; i < s.memoryTrend.size(); i++) {
            if (i > 0) out << ", ";
            out << "{\"elapsed_s\": " << s.memoryTrend[i].elapsedSeconds
                << ", \"working_set_kb\": " << s.memoryTrend[i].workingSetKB << "}";
        }
        out << "]\n";
        out << "  },\n";

        out << "  \"budget_violations\": " << s.budgetViolations << "\n";
        out << "}\n";
        return true;
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
