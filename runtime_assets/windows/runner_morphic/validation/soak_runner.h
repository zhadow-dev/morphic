#ifndef RUNNER_VALIDATION_SOAK_RUNNER_H_
#define RUNNER_VALIDATION_SOAK_RUNNER_H_

#include <string>
#include <vector>

#include "frame_clock.h"

class InteractionSimulator;
class SurfaceManager;
class TestScenarios;

// PHASE 8B.7 — SoakRunner
//
// Continuously cycles through test scenarios for a configurable duration.
// Uses FrameClock subscription to track elapsed time and the TestScenarios
// done-callback chaining to advance to the next scenario.
//
// Usage: construct, call Start(). It subscribes to the clock and auto-cycles
// until duration_minutes elapses, then logs summary and unsubscribes.
//
// This is the 2h soak gate required before temporal authority migration.
// The runner does NOT own the scenarios or simulator — it just orchestrates
// the cycling.  All telemetry is collected by the existing PassiveTelemetry
// subsystems (TickMonitor, ProjectionAuditor, IntegrityAuditor).
class SoakRunner {
 public:
  SoakRunner(FrameClock* clock, TestScenarios* scenarios,
             InteractionSimulator* simulator, SurfaceManager* manager);
  ~SoakRunner();

  SoakRunner(const SoakRunner&) = delete;
  SoakRunner& operator=(const SoakRunner&) = delete;

  // Start cycling scenarios for `duration_minutes`. Non-blocking — returns
  // immediately. Scenarios run asynchronously via clock ticks.
  void Start(double duration_minutes);

  // True while the soak is in progress.
  bool running() const { return running_; }

  // Stats
  int scenarios_completed() const { return scenarios_completed_; }
  int cycles_completed() const { return cycles_completed_; }
  double elapsed_minutes() const;

 private:
  void OnTick(double dt_ms);
  void RunNext();
  void Finish();

  FrameClock* clock_;
  TestScenarios* scenarios_;
  InteractionSimulator* simulator_;
  SurfaceManager* manager_;

  FrameClock::Token token_ = 0;
  bool running_ = false;

  std::vector<std::string> scenario_names_;
  size_t current_index_ = 0;
  int scenarios_completed_ = 0;
  int cycles_completed_ = 0;

  double duration_limit_ms_ = 0.0;
  LARGE_INTEGER start_qpc_{};
  LARGE_INTEGER last_qpc_{};

  bool waiting_for_completion_ = false;

  // Inter-scenario cooldown: wait N ticks between scenarios so the runtime
  // settles (capture released, transaction committed, subscribers quiesced).
  int cooldown_ticks_ = 0;
  static constexpr int kCooldownTicks = 10;  // ~160ms at 16ms cadence
};

#endif  // RUNNER_VALIDATION_SOAK_RUNNER_H_
