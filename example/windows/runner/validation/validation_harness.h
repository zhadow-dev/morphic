#ifndef RUNNER_VALIDATION_VALIDATION_HARNESS_H_
#define RUNNER_VALIDATION_VALIDATION_HARNESS_H_

#include <memory>
#include <string>
#include <vector>

class EventBus;
class FrameClock;
class InteractionRouter;
class SurfaceGraph;
class SurfaceManager;
class SurfaceModel;

class ForensicLogger;
class ActivationCoherencyAuditor;
class IntegrityAuditor;
class OntologyAuditor;  // M2.1C — ontology enforcement layer
class InteractionSimulator;
class ProjectionAuditor;
class SessionReportWriter;
class SoakRunner;
class TestScenarios;
class TickMonitor;
struct RuntimeTelemetry;

// PHASE 7D — ValidationHarness
//
// Top-level coordinator. MorphicRuntime constructs one with a ValidationMode;
// the harness instantiates the sub-modules appropriate for that mode and wires
// them to the runtime's existing services (FrameClock, EventBus, SurfaceModel,
// InteractionRouter, SurfaceManager).
//
// Modes:
//   Disabled            — harness exists but installs nothing; ~zero cost.
//   PassiveTelemetry    — observers only (tick monitor, projection audit,
//                         integrity audit, session report). FrameClock is now
//                         always-on (tick monitor permanently subscribes).
//   InteractiveStress   — PassiveTelemetry + simulator available; scenarios
//                         can be run manually (RunScenario).
//   FullTorture         — InteractiveStress + scenarios auto-run on startup
//                         after a short delay (so initial frames complete).
enum class ValidationMode {
  Disabled,
  PassiveTelemetry,
  InteractiveStress,
  FullTorture,
};

const char* ToString(ValidationMode mode);

class ValidationHarness {
 public:
  ValidationHarness(ValidationMode mode, FrameClock* clock, EventBus* bus,
                    SurfaceModel* model, InteractionRouter* router,
                    SurfaceManager* manager, SurfaceGraph* graph);
  ~ValidationHarness();

  ValidationHarness(const ValidationHarness&) = delete;
  ValidationHarness& operator=(const ValidationHarness&) = delete;

  ValidationMode mode() const { return mode_; }
  const RuntimeTelemetry& telemetry() const { return *telemetry_; }

  bool RunScenario(const std::string& name);
  void RunAllScenarios();

  // PHASE 8B.7 — start continuous soak cycling for `minutes`.
  void StartSoak(double minutes);
  bool soak_running() const;

  // Called by MorphicRuntime::Destroy before subsystems tear down. Writes the
  // session summary while every counter is still readable.
  void FinalizeSession(const std::string& reason);

 private:
  void InstallBusTap();   // hooks runtime events to telemetry counters
  void ScheduleAutoTorture();  // FullTorture mode only
  void OnAutoTortureTick();    // one-shot dispatcher for the above

  ValidationMode mode_;
  FrameClock* clock_;
  EventBus* bus_;
  SurfaceModel* model_;
  InteractionRouter* router_;
  SurfaceManager* manager_;
  SurfaceGraph* graph_;

  std::unique_ptr<RuntimeTelemetry>     telemetry_;
  std::unique_ptr<SessionReportWriter>  report_writer_;
  std::unique_ptr<ForensicLogger>       event_trace_;
  std::unique_ptr<ForensicLogger>       projection_trace_;
  std::unique_ptr<ForensicLogger>       tick_trace_;
  std::unique_ptr<ForensicLogger>       integrity_trace_;
  std::unique_ptr<TickMonitor>          tick_monitor_;
  std::unique_ptr<ProjectionAuditor>    projection_auditor_;
  std::unique_ptr<IntegrityAuditor>     integrity_auditor_;
  std::unique_ptr<ActivationCoherencyAuditor> coherency_auditor_;  // PHASE 10.4B
  std::unique_ptr<OntologyAuditor>      ontology_auditor_;  // M2.1C — constitutional enforcement
  std::unique_ptr<InteractionSimulator> simulator_;
  std::unique_ptr<TestScenarios>        scenarios_;
  std::unique_ptr<SoakRunner>           soak_runner_;

  // Member-owned to avoid the dangling-`this` race: if the one-shot subscription
  // hasn't fired by the time the harness destructs, the dtor unsubscribes it.
  int  auto_torture_token_ = 0;
  bool auto_torture_fired_ = false;

  // PHASE 8B-prep — bus tap (telemetry-counter updater) is now token-owned.
  // Was previously an anonymous Subscribe(); destructor yanks it before the
  // captured ForensicLogger* pointers die.
  int bus_tap_token_ = 0;
};

#endif  // RUNNER_VALIDATION_VALIDATION_HARNESS_H_
