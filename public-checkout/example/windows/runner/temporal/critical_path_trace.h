#ifndef RUNNER_TEMPORAL_CRITICAL_PATH_TRACE_H_
#define RUNNER_TEMPORAL_CRITICAL_PATH_TRACE_H_

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "temporal/frame_epoch.h"

// PHASE 8B.7 — CriticalPathTrace.
//
// Ported from windows/src/debug/tick_coherence_probe.h (the plugin-tree probe).
// That probe's histograms, cost accumulators, projection-latency tracking, and
// bottleneck classifier are PROVEN — this is a reuse, not a rewrite. Two
// deliberate changes from the original:
//
//   1. EPOCH-AWARE, ABI-STABLE schema. Every name describes the epoch model, not
//      the WM_TIMER implementation. "timer_arrivals" → "epoch_cadence";
//      "frame_costs"/"process_frame" → per-phase costs; new dimensions for
//      wake reason, budget pressure, nested depth, and severity. There is no
//      "wm_timer" or "tick" term anywhere — those are FrameClock concepts, not
//      runtime-epoch concepts. If the waitable timer is later replaced by a
//      vsync source these names still hold.
//
//   2. The probe stays MEASUREMENT-ONLY (same rule as the original). It records;
//      it never paces, never replaces a timer, never injects a fix. The
//      FrameScheduler feeds it via EpochTelemetryAdapter — the probe never
//      touches the scheduler.
//
// This type is intentionally tick/epoch-CENTRIC (one cadence histogram, one set
// of cost accumulators). It is NOT fused with the FrameEpoch struct: the adapter
// maps epochs to these recorders so that future projection/presentation/animation
// epochs can each get their OWN CriticalPathTrace instance without entangling
// their schemas.
namespace morphic {

class CriticalPathTrace {
 public:
  // --- Histogram buckets for epoch cadence deltas (epoch-to-epoch wall gap) ---
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

  // --- Cost accumulator for a phase / subsystem ---
  struct CostAccumulator {
    int sampleCount = 0;
    double sumMs = 0.0;
    double peakMs = 0.0;

    void record(double costMs) {
      sampleCount++;
      sumMs += costMs;
      if (costMs > peakMs) peakMs = costMs;
    }

    double avgMs() const { return sampleCount > 0 ? sumMs / sampleCount : 0.0; }
  };

  CriticalPathTrace() {
    QueryPerformanceFrequency(&qpcFreq_);
    lastEpochStart_.QuadPart = 0;
  }

  // ---- Epoch-boundary recorders (called by EpochTelemetryAdapter) ----

  // Record the cadence delta between this epoch's wake and the previous one.
  // Call with the epoch's start QPC (frame_epoch.start_qpc).
  void recordEpochStart(LARGE_INTEGER start) {
    if (lastEpochStart_.QuadPart != 0) {
      const double deltaMs = qpcToMs(start.QuadPart - lastEpochStart_.QuadPart);
      epochCadence_.record(deltaMs);
    }
    lastEpochStart_ = start;
    ++epochCount_;
  }

  // Per-phase durations.
  void recordPreDispatchCost(double ms) { preDispatchCost_.record(ms); }
  void recordInputDispatchCost(double ms) { inputDispatchCost_.record(ms); }
  void recordRuntimeUpdateCost(double ms) { runtimeUpdateCost_.record(ms); }
  void recordEpochDuration(double ms) { epochDuration_.record(ms); }

  // Messages dispatched in the epoch (for queue-pressure visibility).
  void recordMessagesDispatched(int n) {
    messagesTotal_ += n;
    if (n > messagesPeak_) messagesPeak_ = n;
  }

  // Categorical dimensions, counted per epoch.
  void recordWakeReason(EpochWakeReason r) {
    wakeReasonCounts_[static_cast<size_t>(r)]++;
  }
  void recordStarvation(StarvationReason r) {
    if (r != StarvationReason::None) {
      starvationCounts_[static_cast<size_t>(r)]++;
    }
  }
  void recordBudgetPressure(BudgetPressureSource s) {
    if (s != BudgetPressureSource::None) {
      budgetPressureCounts_[static_cast<size_t>(s)]++;
    }
  }
  void recordSeverity(EpochOverrunSeverity s) {
    severityCounts_[static_cast<size_t>(s)]++;
  }
  void recordNestedDepth(int depth) {
    if (depth <= 1) nestedDepth1_++;
    else if (depth == 2) nestedDepth2_++;
    else if (depth == 3) nestedDepth3_++;
    else nestedDepth4plus_++;
    if (depth > nestedDepthPeak_) nestedDepthPeak_ = depth;
  }

  // ---- Pointer→Projection latency (unchanged semantics from the probe) ----
  // The real interaction latency humans feel; tick/epoch timing alone is
  // insufficient (16ms epoch + 45ms projection delay still feels awful).
  void recordPointerEvent() {
    QueryPerformanceCounter(&lastPointerEvent_);
    pointerPending_ = true;
  }

  void recordProjectionComplete() {
    if (!pointerPending_) return;
    pointerPending_ = false;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const double latencyMs = qpcToMs(now.QuadPart - lastPointerEvent_.QuadPart);
    projectionSamples_++;
    projectionSumMs_ += latencyMs;
    if (latencyMs > projectionPeakMs_) projectionPeakMs_ = latencyMs;
    if (projectionHistory_.size() < kMaxProjectionHistory) {
      projectionHistory_.push_back(latencyMs);
    } else {
      projectionHistory_[projectionHistoryHead_] = latencyMs;
      projectionHistoryHead_ = (projectionHistoryHead_ + 1) % kMaxProjectionHistory;
    }
  }

  // ---- Report ----
  uint64_t epochCount() const { return epochCount_; }
  const DeltaHistogram& cadence() const { return epochCadence_; }

  // Classify the dominant bottleneck. Same priority logic as the original probe,
  // re-expressed against epoch phases.
  std::string classifyBottleneck() const {
    if (epochCadence_.totalSamples < 10) return "insufficient_data";

    const double total = static_cast<double>(epochCadence_.totalSamples);
    const double pctOver20 =
        (epochCadence_.ms20to32 + epochCadence_.over32ms) / total * 100.0;
    const double pctOver32 = epochCadence_.over32ms / total * 100.0;

    // Cadence itself consistently late → pacing source (timer resolution / OS).
    if (pctOver20 > 60.0) return "cadence_source_or_os_resolution";

    // RuntimeUpdate phase too long?
    if (runtimeUpdateCost_.sampleCount > 10 && runtimeUpdateCost_.avgMs() > 12.0) {
      return "runtime_update_overload";
    }
    // Dispatch phase too long (input flood / heavy WndProc work)?
    if (inputDispatchCost_.sampleCount > 10 && inputDispatchCost_.avgMs() > 6.0) {
      return "input_dispatch_overload";
    }
    // Nested pumps dominating?
    if (nestedDepthPeak_ > 2) return "nested_pump_interference";
    // Cadence mostly fine but still some slip with cheap phases → queue starvation.
    if (pctOver20 > 30.0 && runtimeUpdateCost_.avgMs() < 6.0 &&
        inputDispatchCost_.avgMs() < 3.0) {
      return "message_queue_starvation";
    }
    if (projectionSamples_ > 10 && projectionPeakMs_ > 50.0) {
      return "projection_pipeline_stall";
    }
    if (pctOver32 > 10.0) return "os_scheduler_pressure";
    return "no_clear_bottleneck";
  }

  // Serialize to JSON. Schema names are the Temporal ABI — stable across
  // scheduler implementation changes.
  bool writeReport(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << "{\n";
    out << "  \"runtime\": \"epoch\",\n";
    out << "  \"epoch_count\": " << epochCount_ << ",\n";

    // Epoch cadence histogram (the spine).
    out << "  \"epoch_cadence\": {\n";
    out << "    \"total_samples\": " << epochCadence_.totalSamples << ",\n";
    out << "    \"under_12ms\": " << epochCadence_.under12ms << ",\n";
    out << "    \"12_to_16ms\": " << epochCadence_.ms12to16 << ",\n";
    out << "    \"16_to_20ms\": " << epochCadence_.ms16to20 << ",\n";
    out << "    \"20_to_32ms\": " << epochCadence_.ms20to32 << ",\n";
    out << "    \"over_32ms\": " << epochCadence_.over32ms << ",\n";
    out << "    \"dominant_bucket\": \"" << epochCadence_.dominantBucket() << "\"\n";
    out << "  },\n";

    // Per-phase costs.
    auto writeCost = [&](const char* name, const CostAccumulator& c) {
      out << "  \"" << name << "\": { \"samples\": " << c.sampleCount
          << ", \"avg_ms\": " << c.avgMs() << ", \"peak_ms\": " << c.peakMs
          << " },\n";
    };
    writeCost("pre_dispatch_ms", preDispatchCost_);
    writeCost("input_dispatch_ms", inputDispatchCost_);
    writeCost("runtime_update_ms", runtimeUpdateCost_);
    writeCost("epoch_duration_ms", epochDuration_);

    // Dispatch volume.
    out << "  \"messages\": { \"total\": " << messagesTotal_
        << ", \"peak_per_epoch\": " << messagesPeak_ << " },\n";

    // Wake reasons.
    out << "  \"wake_reason\": {";
    for (size_t i = 0; i < wakeReasonCounts_.size(); ++i) {
      out << (i ? ", " : " ") << "\""
          << ToString(static_cast<EpochWakeReason>(i)) << "\": "
          << wakeReasonCounts_[i];
    }
    out << " },\n";

    // Starvation reasons (non-None only ever incremented).
    out << "  \"starvation\": {";
    for (size_t i = 0; i < starvationCounts_.size(); ++i) {
      out << (i ? ", " : " ") << "\""
          << ToString(static_cast<StarvationReason>(i)) << "\": "
          << starvationCounts_[i];
    }
    out << " },\n";

    // Budget pressure source.
    out << "  \"budget_pressure\": {";
    for (size_t i = 0; i < budgetPressureCounts_.size(); ++i) {
      out << (i ? ", " : " ") << "\""
          << ToString(static_cast<BudgetPressureSource>(i)) << "\": "
          << budgetPressureCounts_[i];
    }
    out << " },\n";

    // Severity grades.
    out << "  \"severity\": {";
    for (size_t i = 0; i < severityCounts_.size(); ++i) {
      out << (i ? ", " : " ") << "\""
          << ToString(static_cast<EpochOverrunSeverity>(i)) << "\": "
          << severityCounts_[i];
    }
    out << " },\n";

    // Nested-pump depth histogram.
    out << "  \"nested_depth\": { \"depth1\": " << nestedDepth1_
        << ", \"depth2\": " << nestedDepth2_
        << ", \"depth3\": " << nestedDepth3_
        << ", \"depth4plus\": " << nestedDepth4plus_
        << ", \"peak\": " << nestedDepthPeak_ << " },\n";

    // Pointer→projection latency.
    out << "  \"projection_latency\": { \"samples\": " << projectionSamples_
        << ", \"avg_ms\": "
        << (projectionSamples_ > 0 ? projectionSumMs_ / projectionSamples_ : 0.0)
        << ", \"peak_ms\": " << projectionPeakMs_
        << ", \"p99_ms\": " << p99Projection() << " },\n";

    out << "  \"identified_bottleneck\": \"" << classifyBottleneck() << "\"\n";
    out << "}\n";
    return true;
  }

  void reset() {
    epochCadence_ = {};
    preDispatchCost_ = {};
    inputDispatchCost_ = {};
    runtimeUpdateCost_ = {};
    epochDuration_ = {};
    lastEpochStart_.QuadPart = 0;
    epochCount_ = 0;
    messagesTotal_ = 0;
    messagesPeak_ = 0;
    wakeReasonCounts_ = {};
    starvationCounts_ = {};
    budgetPressureCounts_ = {};
    severityCounts_ = {};
    nestedDepth1_ = nestedDepth2_ = nestedDepth3_ = nestedDepth4plus_ = 0;
    nestedDepthPeak_ = 0;
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

  double p99Projection() const {
    if (projectionHistory_.empty()) return 0.0;
    auto sorted = projectionHistory_;
    std::sort(sorted.begin(), sorted.end());
    size_t idx = static_cast<size_t>(sorted.size() * 0.99);
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
  }

  LARGE_INTEGER qpcFreq_{};
  LARGE_INTEGER lastEpochStart_{};
  uint64_t epochCount_ = 0;

  DeltaHistogram epochCadence_;
  CostAccumulator preDispatchCost_;
  CostAccumulator inputDispatchCost_;
  CostAccumulator runtimeUpdateCost_;
  CostAccumulator epochDuration_;

  long long messagesTotal_ = 0;
  int messagesPeak_ = 0;

  // Sized to each enum's cardinality (+ small slack is unnecessary — exact).
  std::array<int, 5> wakeReasonCounts_{};       // EpochWakeReason
  std::array<int, 5> starvationCounts_{};       // StarvationReason
  std::array<int, 5> budgetPressureCounts_{};   // BudgetPressureSource
  std::array<int, 5> severityCounts_{};         // EpochOverrunSeverity

  int nestedDepth1_ = 0, nestedDepth2_ = 0, nestedDepth3_ = 0,
      nestedDepth4plus_ = 0;
  int nestedDepthPeak_ = 0;

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

#endif  // RUNNER_TEMPORAL_CRITICAL_PATH_TRACE_H_
