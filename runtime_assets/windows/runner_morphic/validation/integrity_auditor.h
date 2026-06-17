#ifndef RUNNER_VALIDATION_INTEGRITY_AUDITOR_H_
#define RUNNER_VALIDATION_INTEGRITY_AUDITOR_H_

#include "frame_clock.h"
#include "runtime_events.h"

class EventBus;
class ForensicLogger;
class InteractionRouter;
struct RuntimeTelemetry;
class SurfaceGraph;
class SurfaceManager;
class SurfaceModel;
class SurfaceShell;

// PHASE 7D — IntegrityAuditor
//
// Periodically (every N ticks) validates SurfaceModel invariants. Also runs an
// immediate audit on lifecycle events (SurfaceCreated/Destroyed) so churn is
// captured.
//
// Invariants checked:
//   - active_ surface, if non-null, is in z_order_
//   - focused_ surface, if non-null, is in z_order_
//   - z_order_ has no duplicates
//   - every z_order_ entry has bounds_ entry
//   - every z_order_ entry has a non-null HWND
//   - active_/focused_ counts ≤ 1
//
// On any failure: logs structured event, increments integrity_failures, calls
// VALIDATION_ASSERT in debug.
class IntegrityAuditor {
 public:
  // Run an integrity audit every kAuditEveryNTicks frame ticks. 60 ≈ once/sec.
  static constexpr int kAuditEveryNTicks = 60;

  IntegrityAuditor(FrameClock* clock, EventBus* bus, SurfaceManager* manager,
                   SurfaceModel* model, InteractionRouter* router,
                   SurfaceGraph* graph, RuntimeTelemetry* telemetry,
                   ForensicLogger* trace);
  ~IntegrityAuditor();

  IntegrityAuditor(const IntegrityAuditor&) = delete;
  IntegrityAuditor& operator=(const IntegrityAuditor&) = delete;

 private:
  void OnTick(double dt_ms);
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);
  void RunAudit(const char* reason);

  FrameClock* clock_;
  EventBus* bus_;
  SurfaceManager* manager_;
  SurfaceModel* model_;
  InteractionRouter* router_;
  SurfaceGraph* graph_;
  RuntimeTelemetry* telemetry_;
  ForensicLogger* trace_;

  FrameClock::Token token_ = 0;
  int bus_token_ = 0;  // I-A2 closure
  int tick_count_ = 0;
};

#endif  // RUNNER_VALIDATION_INTEGRITY_AUDITOR_H_
