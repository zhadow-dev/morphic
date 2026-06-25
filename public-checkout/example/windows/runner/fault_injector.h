#ifndef RUNNER_FAULT_INJECTOR_H_
#define RUNNER_FAULT_INJECTOR_H_

#include <vector>

#include "frame_clock.h"

class EventBus;
class InteractionRouter;
class SurfaceModel;

// PHASE 7C-T — Opt-in chaos for torture-testing the temporal runtime.
//
// None of these are installed by default. MorphicRuntime instantiates this and
// optionally calls one or more Install*() per compile-time flags in
// morphic_runtime.cpp. Each install is observable in the forensic log so you
// can confirm exactly which faults are active for a given session.
//
// Test mapping:
//   InstallDummySubscribers(N)        → Test 4A — multi-subscriber clock
//   InstallSlowSubscriber(sleep_ms)   → Test 4B — slow-subscriber poisoning
//   InstallReentrancyHook()           → Test 6A — R4 guard validation
//
// All faults are removed at destruction (RemoveAll). Lifetime: same as the
// runtime, so faults span the entire process.
class FaultInjector {
 public:
  FaultInjector(FrameClock* clock, EventBus* bus, SurfaceModel* model,
                InteractionRouter* router);
  ~FaultInjector();

  FaultInjector(const FaultInjector&) = delete;
  FaultInjector& operator=(const FaultInjector&) = delete;

  // Subscribe N no-op callbacks to the clock. Confirms multi-subscriber tick
  // dispatch is correct (snapshot iteration, no ordering bugs, no leaks).
  void InstallDummySubscribers(int count);

  // Subscribe ONE callback that Sleep()s every tick. Validates that a poorly
  // behaved subscriber degrades the runtime predictably (peak_tick_interval_ms
  // rises; runtime does not collapse / recurse / lose ticks catastrophically).
  void InstallSlowSubscriber(int sleep_ms);

  // Install a SurfaceModel test-projection callback that re-enters SetBounds.
  // Should drive reentrant_drops > 0 on the next interaction without crashing
  // or corrupting bounds_. Validates R4 guard is real, not theoretical.
  void InstallReentrancyHook();

  void RemoveAll();

 private:
  FrameClock* clock_;
  EventBus* bus_;
  SurfaceModel* model_;
  InteractionRouter* router_;
  std::vector<FrameClock::Token> clock_tokens_;
  bool reentrancy_installed_ = false;
};

#endif  // RUNNER_FAULT_INJECTOR_H_
