#ifndef RUNNER_VALIDATION_RUNTIME_TELEMETRY_H_
#define RUNNER_VALIDATION_RUNTIME_TELEMETRY_H_

#include <atomic>

// PHASE 7D — RuntimeTelemetry
//
// Shared counters / gauges populated by every harness subsystem. Atomics make
// snapshot reads safe from a reporting thread without locks (and are basically
// free on x86). Increment from the UI thread; read from anywhere.
//
// Reset by ValidationHarness at start; never reset mid-session.
struct RuntimeTelemetry {
  // --- Frame clock ---
  std::atomic<int>    tick_count{0};
  std::atomic<int>    tick_starvation_count{0};   // ticks where dt > kStarvationThresholdMs
  std::atomic<double> last_tick_interval_ms{0.0};
  std::atomic<double> peak_tick_interval_ms{0.0};
  std::atomic<double> sum_tick_interval_ms{0.0};
  std::atomic<int>    subscriber_peak{0};
  std::atomic<double> peak_subscriber_duration_ms{0.0};

  // PHASE 8A.2 — tick interval histogram. Buckets are coarse on purpose:
  //   <12ms  : faster than vsync target — usually catch-up after starvation
  //   12-20  : healthy band around 16ms
  //   20-32  : noticeable hitching at 60Hz; one frame skipped
  //   >=32   : multi-frame stall — visible jank
  // Lets us verify the timeBeginPeriod(1) fix actually moved the distribution
  // and gives a shape for future jitter audits without polluting the trace.
  std::atomic<int> tick_bucket_under_12ms{0};
  std::atomic<int> tick_bucket_12_20ms{0};
  std::atomic<int> tick_bucket_20_32ms{0};
  std::atomic<int> tick_bucket_over_32ms{0};

  // --- Interactions ---
  std::atomic<int> interactions_started{0};
  std::atomic<int> interactions_ended{0};
  std::atomic<int> interactions_cancelled{0};
  std::atomic<int> capture_acquisitions{0};
  std::atomic<int> capture_releases{0};

  // --- Projection / geometry ---
  std::atomic<int> projections{0};
  std::atomic<int> projections_dropped{0};       // coalesce drop (pending == last)
  std::atomic<int> projection_storms{0};         // > kStormThreshold per single tick
  std::atomic<int> reentrant_drops{0};           // R4 guard fires

  // --- Pointer / input ---
  std::atomic<int>    pointer_events{0};
  std::atomic<double> peak_velocity_px_per_s{0.0};

  // --- Audit ---
  std::atomic<int>  audit_ticks{0};
  std::atomic<int>  audit_warnings{0};
  std::atomic<long> peak_target_presented_lag_px{0};
  std::atomic<long> peak_presented_native_lag_px{0};

  // --- Integrity ---
  std::atomic<int> integrity_checks{0};
  std::atomic<int> integrity_failures{0};

  // --- Surface lifecycle ---
  std::atomic<int> surfaces_created{0};
  std::atomic<int> surfaces_destroyed{0};
  std::atomic<int> surfaces_live{0};

  // --- Transactions ---
  std::atomic<int> txn_begun{0};
  std::atomic<int> txn_committed{0};

  // Thresholds used by subsystems to flag pathological telemetry.
  static constexpr double kStarvationThresholdMs = 32.0;
  static constexpr int    kStormThreshold = 4;
};

#endif  // RUNNER_VALIDATION_RUNTIME_TELEMETRY_H_
