#ifndef RUNNER_VALIDATION_PROJECTION_AUDITOR_H_
#define RUNNER_VALIDATION_PROJECTION_AUDITOR_H_

#include <windows.h>

#include "runtime_events.h"

class EventBus;
class ForensicLogger;
class InteractionRouter;
struct RuntimeTelemetry;
class SurfaceModel;
class SurfaceShell;

// PHASE 7D — ProjectionAuditor (formerly InteractionAudit)
//
// Verifies the three-layer model stays coherent on every projected tick:
//   target ↔ presented  — expected lag while smoothing
//   presented ↔ native  — BUG if non-zero (our SetBounds vs HWND disagree)
//
// PHASE 8B.6.6 — extended with native projection coherence audits:
//   I-P1: child HWND rect must match projected client rect (every tick)
//   I-P2: shell client rect must match semantic projected rect (every tick)
//   I-P3: shell must intersect at least one monitor work area (every tick)
//   Monitor affinity + DPI tracking (every tick)
//   DWM cloaking + extended frame bounds (every 60 ticks)
//   Parent/child ownership integrity (every 60 ticks)
//   Child HWND identity stability (every tick via LayoutChild)
//
// Per-tick log only on threshold breach (kWarnThresholdPx). Per-interaction
// summary on InteractionEnded. Populates RuntimeTelemetry counters.
class ProjectionAuditor {
 public:
  static constexpr long kWarnThresholdPx = 8;
  // PHASE 8B.6.6 — child projection tolerance (physical px).
  static constexpr long kChildDivergenceThresholdPx = 2;
  // How often (in ticks) to run expensive DWM/ownership audits.
  static constexpr int kDeepAuditInterval = 60;

  ProjectionAuditor(EventBus* bus, InteractionRouter* router,
                    SurfaceModel* model, RuntimeTelemetry* telemetry,
                    ForensicLogger* trace);
  ~ProjectionAuditor();

  ProjectionAuditor(const ProjectionAuditor&) = delete;
  ProjectionAuditor& operator=(const ProjectionAuditor&) = delete;

 private:
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);
  void AuditTick(SurfaceShell* surface);
  void Reset(SurfaceShell* surface);
  void Summarize(SurfaceShell* surface);

  // PHASE 8B.6.6 — native projection coherence audits.
  void AuditChildProjection(SurfaceShell* surface);
  void AuditShellCoherence(SurfaceShell* surface);
  void AuditMonitorAffinity(SurfaceShell* surface);
  void AuditDeep(SurfaceShell* surface);  // cloaking, ownership, DWM bounds

  EventBus* bus_;
  InteractionRouter* router_;
  SurfaceModel* model_;
  RuntimeTelemetry* telemetry_;
  ForensicLogger* trace_;

  int  ticks_audited_ = 0;
  long peak_target_presented_px_ = 0;
  long peak_presented_native_px_ = 0;
  int  warn_count_ = 0;
  int  bus_token_ = 0;  // I-A2 closure

  // PHASE 8B.6.6 — child projection coherence tracking.
  int  child_divergence_count_ = 0;
  long peak_child_divergence_px_ = 0;
  int  shell_divergence_count_ = 0;
  int  visibility_fail_count_ = 0;
  int  deep_audit_counter_ = 0;  // counts ticks for periodic deep audit
};


#endif  // RUNNER_VALIDATION_PROJECTION_AUDITOR_H_
