#include "validation/tick_monitor.h"

#include "validation/forensic_logger.h"
#include "validation/runtime_telemetry.h"

namespace {

double ElapsedMs(LARGE_INTEGER a, LARGE_INTEGER b) {
  LARGE_INTEGER f{};
  QueryPerformanceFrequency(&f);
  if (f.QuadPart == 0) return 0.0;
  return (b.QuadPart - a.QuadPart) * 1000.0 / static_cast<double>(f.QuadPart);
}

// C++17 std::atomic<double> has no fetch_add (added in C++20). CAS loop is the
// portable accumulator. Single-writer UI-thread cost is one CAS per call.
void AtomicAddDouble(std::atomic<double>& a, double delta) {
  double cur = a.load(std::memory_order_relaxed);
  while (!a.compare_exchange_weak(cur, cur + delta, std::memory_order_relaxed)) {
    // cur is updated to the latest value by compare_exchange_weak on failure
  }
}

}  // namespace

TickMonitor::TickMonitor(FrameClock* clock, RuntimeTelemetry* telemetry,
                         ForensicLogger* tick_trace)
    : clock_(clock), telemetry_(telemetry), trace_(tick_trace) {
  if (!clock_) return;
  token_ = clock_->Subscribe([this](double dt_ms) { OnTick(dt_ms); });
}

TickMonitor::~TickMonitor() {
  if (clock_ && token_ != 0) {
    clock_->Unsubscribe(token_);
  }
}

void TickMonitor::OnTick(double dt_ms) {
  // Independent measurement: we trust the clock's dt but also QPC-stamp the
  // tick ourselves, so disagreement between the two is itself a signal.
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  const double own_dt = (last_tick_qpc_.QuadPart != 0)
                             ? ElapsedMs(last_tick_qpc_, now)
                             : 0.0;
  last_tick_qpc_ = now;

  if (!telemetry_) return;
  const int n = telemetry_->tick_count.fetch_add(1) + 1;
  telemetry_->last_tick_interval_ms.store(dt_ms);
  AtomicAddDouble(telemetry_->sum_tick_interval_ms, dt_ms);
  // Compare-and-swap-update on peak: simple, racy is fine since UI-thread-only.
  double prev_peak = telemetry_->peak_tick_interval_ms.load();
  if (dt_ms > prev_peak) telemetry_->peak_tick_interval_ms.store(dt_ms);
  if (dt_ms > RuntimeTelemetry::kStarvationThresholdMs && n > 1) {
    telemetry_->tick_starvation_count.fetch_add(1);
  }
  // PHASE 8A.2 — bucket the interval. Skip the very first tick (n==1)
  // because its dt_ms is 0 (no previous tick to subtract from).
  if (n > 1) {
    if (dt_ms < 12.0) {
      telemetry_->tick_bucket_under_12ms.fetch_add(1);
    } else if (dt_ms < 20.0) {
      telemetry_->tick_bucket_12_20ms.fetch_add(1);
    } else if (dt_ms < 32.0) {
      telemetry_->tick_bucket_20_32ms.fetch_add(1);
    } else {
      telemetry_->tick_bucket_over_32ms.fetch_add(1);
    }
  }

  const int subs = static_cast<int>(clock_ ? clock_->subscriber_count() : 0);
  if (subs > telemetry_->subscriber_peak.load()) {
    telemetry_->subscriber_peak.store(subs);
  }

  // Forensic emit every tick. The tick_trace.ndjson can grow large in long
  // sessions — the user opts in by enabling validation mode.
  if (trace_) {
    JsonLine line;
    line.Str("kind", "tick")
        .Int("n", n)
        .Num("clock_dt_ms", dt_ms)
        .Num("own_dt_ms", own_dt)
        .Int("subs", subs);
    trace_->LogEvent(line);
  }
}
