#pragma once

#include "../core/interaction_phase.h"

#include <windows.h>
#include <array>
#include <string>
#include <fstream>
#include <cmath>
#include <algorithm>

namespace morphic {

// Phase 8B.6 — Tick Coherence Probe.
//
// Answers the question: WHY is observed cadence ~25–28ms instead of ~16ms?
//
// Possible causes (must be measured, NOT guessed):
//   1. Timer resolution (WM_TIMER minimum granularity)
//   2. Message queue starvation (timer messages delayed behind other messages)
//   3. Synchronous handler duration (processFrame takes >16ms)
//   4. Nested pump interference (DispatchMessage re-entrancy)
//   5. Event fanout cost (RuntimeEventBus dispatch time)
//   6. SetWindowPos cost (DeferWindowPos + EndDeferWindowPos stalls)
//   7. DWM composition throttling
//   8. Flutter engine pauses
//   9. Validator/auditor overhead
//  10. WM_TIMER coalescing (OS merges timer messages)
//  11. OS scheduler pressure
//
// Additionally tracks:
//   Pointer → Projection Latency
//   The real interaction latency: from pointer event timestamp to
//   projected SetWindowPos timestamp. Tick timing alone is insufficient
//   because 16ms tick + 45ms projection delay still feels awful.
//
// CRITICAL RULE:
//   This probe is MEASUREMENT ONLY.
//   NO timer replacement. NO timeBeginPeriod. NO threading changes.
//   NO speculative fixes. Measure first. Always.
class TickCoherenceProbe {
public:
    // --- Histogram buckets for timer arrival deltas ---
    struct DeltaHistogram {
        int under12ms = 0;
        int ms12to16 = 0;
        int ms16to20 = 0;
        int ms20to32 = 0;
        int over32ms = 0;
        int totalSamples = 0;

        void record(double deltaMs) {
            totalSamples++;
            if (deltaMs < 12.0) under12ms++;
            else if (deltaMs < 16.0) ms12to16++;
            else if (deltaMs < 20.0) ms16to20++;
            else if (deltaMs < 32.0) ms20to32++;
            else over32ms++;
        }

        std::string dominantBucket() const {
            int maxVal = (std::max)({under12ms, ms12to16, ms16to20, ms20to32, over32ms});
            if (maxVal == under12ms) return "<12ms";
            if (maxVal == ms12to16) return "12-16ms";
            if (maxVal == ms16to20) return "16-20ms";
            if (maxVal == ms20to32) return "20-32ms";
            return ">32ms";
        }
    };

    // --- Cost accumulator for a specific subsystem ---
    struct CostAccumulator {
        int sampleCount = 0;
        double sumMs = 0.0;
        double peakMs = 0.0;

        void record(double costMs) {
            sampleCount++;
            sumMs += costMs;
            if (costMs > peakMs) peakMs = costMs;
        }

        double avgMs() const {
            return sampleCount > 0 ? sumMs / sampleCount : 0.0;
        }
    };

    // --- Pointer→Projection latency sample ---
    struct ProjectionLatencySample {
        double totalMs = 0.0;     // pointer event → SetWindowPos complete
        InteractionPhase phase = InteractionPhase::Idle;
    };

    // --- Full coherence report ---
    struct CoherenceReport {
        // Timer arrival deltas
        DeltaHistogram timerArrivals;
        DeltaHistogram frameCosts;

        // Subsystem costs
        CostAccumulator processFrameCost;
        CostAccumulator flushPositionsCost;
        CostAccumulator eventFanoutCost;
        CostAccumulator governanceCost;
        CostAccumulator auditCost;
        CostAccumulator commitCycleCost;

        // Message pump diagnostics
        int maxReentrancyDepth = 0;
        int reentrancyEvents = 0;

        // Pointer→Projection latency
        int projectionSamples = 0;
        double avgProjectionLatencyMs = 0.0;
        double peakProjectionLatencyMs = 0.0;
        double p99ProjectionLatencyMs = 0.0;

        // Bottleneck classification
        std::string identifiedBottleneck;
    };

    TickCoherenceProbe() {
        QueryPerformanceFrequency(&qpcFreq_);
        lastTimerArrival_.QuadPart = 0;
    }

    // --- Timer arrival recording ---
    // Call from TimerProc BEFORE any work begins.
    void recordTimerArrival() {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        if (lastTimerArrival_.QuadPart != 0) {
            double deltaMs = qpcToMs(now.QuadPart - lastTimerArrival_.QuadPart);
            timerArrivals_.record(deltaMs);
        }
        lastTimerArrival_ = now;
    }

    // --- Frame cost recording ---
    // Call with the measured processFrame() duration.
    void recordFrameCost(double costMs) {
        frameCosts_.record(costMs);
        processFrameCost_.record(costMs);
    }

    // --- Subsystem cost recording ---
    // Each subsystem calls its recorder with QPC-measured duration.
    void recordFlushPositionsCost(double costMs) {
        flushPositionsCost_.record(costMs);
    }

    void recordEventFanoutCost(double costMs) {
        eventFanoutCost_.record(costMs);
    }

    void recordGovernanceCost(double costMs) {
        governanceCost_.record(costMs);
    }

    void recordAuditCost(double costMs) {
        auditCost_.record(costMs);
    }

    void recordCommitCycleCost(double costMs) {
        commitCycleCost_.record(costMs);
    }

    // --- Re-entrancy detection ---
    // Call when entering/exiting DispatchMessage.
    void pushReentrancy() {
        currentReentrancy_++;
        if (currentReentrancy_ > 1) {
            reentrancyEvents_++;
        }
        if (currentReentrancy_ > maxReentrancy_) {
            maxReentrancy_ = currentReentrancy_;
        }
    }

    void popReentrancy() {
        if (currentReentrancy_ > 0) currentReentrancy_--;
    }

    // --- Pointer→Projection Latency ---
    //
    // This is the real interaction latency humans feel.
    // Tick timing alone is insufficient because:
    //   16ms tick + 45ms projection delay still feels awful.
    //
    // Call recordPointerEvent() when a pointer input arrives.
    // Call recordProjectionComplete() when SetWindowPos finishes for that input.
    void recordPointerEvent() {
        QueryPerformanceCounter(&lastPointerEvent_);
        pointerPending_ = true;
    }

    void recordProjectionComplete(InteractionPhase phase) {
        if (!pointerPending_) return;
        pointerPending_ = false;

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double latencyMs = qpcToMs(now.QuadPart - lastPointerEvent_.QuadPart);

        projectionSamples_++;
        projectionSumMs_ += latencyMs;
        if (latencyMs > projectionPeakMs_) projectionPeakMs_ = latencyMs;

        // Rolling buffer for p99
        if (projectionHistory_.size() < kMaxProjectionHistory) {
            projectionHistory_.push_back(latencyMs);
        } else {
            projectionHistory_[projectionHistoryHead_] = latencyMs;
            projectionHistoryHead_ = (projectionHistoryHead_ + 1) % kMaxProjectionHistory;
        }
    }

    // --- Report generation ---
    CoherenceReport report() const {
        CoherenceReport r;
        r.timerArrivals = timerArrivals_;
        r.frameCosts = frameCosts_;
        r.processFrameCost = processFrameCost_;
        r.flushPositionsCost = flushPositionsCost_;
        r.eventFanoutCost = eventFanoutCost_;
        r.governanceCost = governanceCost_;
        r.auditCost = auditCost_;
        r.commitCycleCost = commitCycleCost_;
        r.maxReentrancyDepth = maxReentrancy_;
        r.reentrancyEvents = reentrancyEvents_;

        // Projection latency
        r.projectionSamples = projectionSamples_;
        r.avgProjectionLatencyMs = projectionSamples_ > 0 ? projectionSumMs_ / projectionSamples_ : 0.0;
        r.peakProjectionLatencyMs = projectionPeakMs_;

        if (!projectionHistory_.empty()) {
            auto sorted = projectionHistory_;
            std::sort(sorted.begin(), sorted.end());
            size_t idx = static_cast<size_t>(sorted.size() * 0.99);
            if (idx >= sorted.size()) idx = sorted.size() - 1;
            r.p99ProjectionLatencyMs = sorted[idx];
        }

        // Classify bottleneck
        r.identifiedBottleneck = classifyBottleneck();
        return r;
    }

    // --- Serialize report to JSON ---
    bool writeReport(const std::string& path) const {
        std::ofstream out(path);
        if (!out.is_open()) return false;

        auto r = report();
        out << "{\n";

        // Timer arrival histogram
        out << "  \"timer_arrivals\": {\n";
        out << "    \"total_samples\": " << r.timerArrivals.totalSamples << ",\n";
        out << "    \"under_12ms\": " << r.timerArrivals.under12ms << ",\n";
        out << "    \"12_to_16ms\": " << r.timerArrivals.ms12to16 << ",\n";
        out << "    \"16_to_20ms\": " << r.timerArrivals.ms16to20 << ",\n";
        out << "    \"20_to_32ms\": " << r.timerArrivals.ms20to32 << ",\n";
        out << "    \"over_32ms\": " << r.timerArrivals.over32ms << ",\n";
        out << "    \"dominant_bucket\": \"" << r.timerArrivals.dominantBucket() << "\"\n";
        out << "  },\n";

        // Frame cost histogram
        out << "  \"frame_costs\": {\n";
        out << "    \"total_samples\": " << r.frameCosts.totalSamples << ",\n";
        out << "    \"under_12ms\": " << r.frameCosts.under12ms << ",\n";
        out << "    \"12_to_16ms\": " << r.frameCosts.ms12to16 << ",\n";
        out << "    \"16_to_20ms\": " << r.frameCosts.ms16to20 << ",\n";
        out << "    \"20_to_32ms\": " << r.frameCosts.ms20to32 << ",\n";
        out << "    \"over_32ms\": " << r.frameCosts.over32ms << "\n";
        out << "  },\n";

        // Subsystem costs
        auto writeCost = [&](const char* name, const CostAccumulator& c) {
            out << "  \"" << name << "\": {"
                << " \"samples\": " << c.sampleCount
                << ", \"avg_ms\": " << c.avgMs()
                << ", \"peak_ms\": " << c.peakMs
                << " },\n";
        };
        writeCost("process_frame", r.processFrameCost);
        writeCost("flush_positions", r.flushPositionsCost);
        writeCost("event_fanout", r.eventFanoutCost);
        writeCost("governance", r.governanceCost);
        writeCost("audit", r.auditCost);
        writeCost("commit_cycle", r.commitCycleCost);

        // Re-entrancy
        out << "  \"reentrancy\": {"
            << " \"max_depth\": " << r.maxReentrancyDepth
            << ", \"events\": " << r.reentrancyEvents
            << " },\n";

        // Pointer→Projection latency
        out << "  \"projection_latency\": {\n";
        out << "    \"samples\": " << r.projectionSamples << ",\n";
        out << "    \"avg_ms\": " << r.avgProjectionLatencyMs << ",\n";
        out << "    \"peak_ms\": " << r.peakProjectionLatencyMs << ",\n";
        out << "    \"p99_ms\": " << r.p99ProjectionLatencyMs << "\n";
        out << "  },\n";

        // Bottleneck
        out << "  \"identified_bottleneck\": \"" << r.identifiedBottleneck << "\"\n";
        out << "}\n";
        return true;
    }

    void reset() {
        timerArrivals_ = {};
        frameCosts_ = {};
        processFrameCost_ = {};
        flushPositionsCost_ = {};
        eventFanoutCost_ = {};
        governanceCost_ = {};
        auditCost_ = {};
        commitCycleCost_ = {};
        lastTimerArrival_.QuadPart = 0;
        maxReentrancy_ = 0;
        currentReentrancy_ = 0;
        reentrancyEvents_ = 0;
        pointerPending_ = false;
        projectionSamples_ = 0;
        projectionSumMs_ = 0.0;
        projectionPeakMs_ = 0.0;
        projectionHistory_.clear();
        projectionHistoryHead_ = 0;
    }

private:
    double qpcToMs(int64_t ticks) const {
        return (qpcFreq_.QuadPart > 0)
            ? ticks * 1000.0 / static_cast<double>(qpcFreq_.QuadPart)
            : 0.0;
    }

    std::string classifyBottleneck() const {
        // Priority-ordered bottleneck classification based on evidence.
        //
        // If the dominant timer arrival bucket is 20-32ms, the clock itself
        // is the primary bottleneck (WM_TIMER resolution / coalescing).
        //
        // If timer arrivals are tight but frame costs are high, then
        // synchronous handler work is the bottleneck.
        //
        // If both are reasonable but projection latency is high, the
        // projection pipeline (SetWindowPos/DWM) is stalling.

        if (timerArrivals_.totalSamples < 10) return "insufficient_data";

        double timerTotal = static_cast<double>(timerArrivals_.totalSamples);
        double pctOver20 = (timerArrivals_.ms20to32 + timerArrivals_.over32ms) / timerTotal * 100.0;
        double pctOver32Timer = timerArrivals_.over32ms / timerTotal * 100.0;

        // Check: Is WM_TIMER itself consistently late?
        if (pctOver20 > 60.0) {
            return "timer_resolution_or_coalescing";
        }

        // Check: Is processFrame() taking too long?
        if (processFrameCost_.sampleCount > 10 && processFrameCost_.avgMs() > 16.0) {
            // Drill into which subsystem
            if (flushPositionsCost_.avgMs() > 4.0) return "setwindowpos_stall";
            if (commitCycleCost_.avgMs() > 8.0) return "commit_cycle_overload";
            if (governanceCost_.avgMs() > 4.0) return "governance_overhead";
            if (auditCost_.avgMs() > 4.0) return "audit_overhead";
            if (eventFanoutCost_.avgMs() > 4.0) return "event_fanout_cost";
            return "synchronous_handler_duration";
        }

        // Check: Are there re-entrancy issues?
        if (maxReentrancy_ > 2) return "nested_pump_interference";

        // Check: Timer mostly on time but still degraded?
        if (pctOver20 > 30.0 && processFrameCost_.avgMs() < 8.0) {
            return "message_queue_starvation";
        }

        // Check: projection latency is disproportionate?
        if (projectionSamples_ > 10 && projectionPeakMs_ > 50.0) {
            return "projection_pipeline_stall";
        }

        // Timer spikes without clear handler cause
        if (pctOver32Timer > 10.0) {
            return "os_scheduler_pressure";
        }

        return "no_clear_bottleneck";
    }

    LARGE_INTEGER qpcFreq_{};
    LARGE_INTEGER lastTimerArrival_{};

    DeltaHistogram timerArrivals_;
    DeltaHistogram frameCosts_;

    CostAccumulator processFrameCost_;
    CostAccumulator flushPositionsCost_;
    CostAccumulator eventFanoutCost_;
    CostAccumulator governanceCost_;
    CostAccumulator auditCost_;
    CostAccumulator commitCycleCost_;

    int maxReentrancy_ = 0;
    int currentReentrancy_ = 0;
    int reentrancyEvents_ = 0;

    // Pointer→Projection latency
    LARGE_INTEGER lastPointerEvent_{};
    bool pointerPending_ = false;
    int projectionSamples_ = 0;
    double projectionSumMs_ = 0.0;
    double projectionPeakMs_ = 0.0;
    static constexpr size_t kMaxProjectionHistory = 500;
    std::vector<double> projectionHistory_;
    size_t projectionHistoryHead_ = 0;
};

}  // namespace morphic
