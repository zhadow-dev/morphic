#ifndef RUNNER_VALIDATION_TICK_MONITOR_H_
#define RUNNER_VALIDATION_TICK_MONITOR_H_

#include "frame_clock.h"

struct RuntimeTelemetry;
class ForensicLogger;

// PHASE 7D — TickMonitor
//
// Permanently subscribes to FrameClock and records every tick to telemetry +
// the tick trace forensic log. Side effect: by subscribing permanently, the
// clock becomes always-on (not lazy-idle). That is the explicit cost of
// passive telemetry; ValidationMode::Disabled skips this subscription.
//
// Detects:
//   - tick interval (last + peak + sum/count for average)
//   - tick starvation (> kStarvationThresholdMs)
//   - subscriber-callback duration (own tick handler — proxy for system load)
class TickMonitor {
 public:
  TickMonitor(FrameClock* clock, RuntimeTelemetry* telemetry,
              ForensicLogger* tick_trace);
  ~TickMonitor();

  TickMonitor(const TickMonitor&) = delete;
  TickMonitor& operator=(const TickMonitor&) = delete;

 private:
  void OnTick(double dt_ms);

  FrameClock* clock_;
  RuntimeTelemetry* telemetry_;
  ForensicLogger* trace_;
  FrameClock::Token token_ = 0;
  LARGE_INTEGER last_tick_qpc_{};
};

#endif  // RUNNER_VALIDATION_TICK_MONITOR_H_
