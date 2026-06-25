#ifndef RUNNER_VALIDATION_ACTIVATION_COHERENCY_AUDITOR_H_
#define RUNNER_VALIDATION_ACTIVATION_COHERENCY_AUDITOR_H_

#include <windows.h>

#include "frame_clock.h"
#include "runtime_events.h"

class EventBus;
class SurfaceModel;
class SurfaceShell;

// PHASE 10.4B — ActivationCoherencyAuditor.
//
// Validates that SEMANTIC authority (SurfaceModel) and NATIVE projection (Win32)
// AGREE on activation + z-order. Symptoms that drove 10.4 (Alt+Tab drift, z
// divergence, titlebar/body split-brain) are exactly semantic↔native disagreement;
// this auditor makes that disagreement OBSERVABLE.
//
// It is RUNTIME-SIDE and KIND-AGNOSTIC: it reads ONLY SurfaceModel + Win32
// (GetForegroundWindow, GW_OWNER owner chains, HWND z via GW_HWNDNEXT). It needs NO
// SurfaceKind → it lives in validation/ and the firewall holds.
//
// It is STRICTLY LOG-ONLY: it OBSERVES, it never corrects/mutates/self-heals
// (auditors observe, they do not govern — otherwise hidden runtime authority
// emerges). Checks run on SurfaceActivated + every N ticks.
//
// Checks:
//   [FOREGROUND DIVERGENCE] — foreground HWND is neither the semantic-active
//        surface nor an owned window of it (owner chain).
//   [ZORDER FAIL] — semantic top-z surface (z_order().front()) is not the topmost
//        of OUR non-topmost surfaces in actual HWND z.
class ActivationCoherencyAuditor {
 public:
  static constexpr int kAuditEveryNTicks = 60;  // ≈ once/sec

  ActivationCoherencyAuditor(FrameClock* clock, EventBus* bus,
                             SurfaceModel* model);
  ~ActivationCoherencyAuditor();

  ActivationCoherencyAuditor(const ActivationCoherencyAuditor&) = delete;
  ActivationCoherencyAuditor& operator=(const ActivationCoherencyAuditor&) = delete;

 private:
  void OnTick(double dt_ms);
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);
  void Audit(const char* reason);

  FrameClock* clock_;
  EventBus* bus_;
  SurfaceModel* model_;

  FrameClock::Token token_ = 0;
  int bus_token_ = 0;
  int tick_count_ = 0;
};

#endif  // RUNNER_VALIDATION_ACTIVATION_COHERENCY_AUDITOR_H_
