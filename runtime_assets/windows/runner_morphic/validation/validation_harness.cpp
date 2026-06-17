#include "validation/validation_harness.h"

#include <utility>

#include "forensic_trace.h"
#include "frame_clock.h"
#include "interaction_router.h"
#include "runtime_events.h"
#include "surface_manager.h"
#include "surface_model.h"
#include "surface_shell.h"
#include "validation/forensic_logger.h"
#include "validation/activation_coherency_auditor.h"
#include "validation/integrity_auditor.h"
#include "validation/interaction_simulator.h"
#include "validation/ontology_auditor.h"
#include "validation/projection_auditor.h"
#include "validation/runtime_telemetry.h"
#include "validation/session_report_writer.h"
#include "validation/soak_runner.h"
#include "validation/test_scenarios.h"
#include "validation/tick_monitor.h"

const char* ToString(ValidationMode mode) {
  switch (mode) {
    case ValidationMode::Disabled:          return "Disabled";
    case ValidationMode::PassiveTelemetry:  return "PassiveTelemetry";
    case ValidationMode::InteractiveStress: return "InteractiveStress";
    case ValidationMode::FullTorture:       return "FullTorture";
  }
  return "?";
}

ValidationHarness::ValidationHarness(ValidationMode mode, FrameClock* clock,
                                     EventBus* bus, SurfaceModel* model,
                                     InteractionRouter* router,
                                     SurfaceManager* manager,
                                     SurfaceGraph* graph)
    : mode_(mode), clock_(clock), bus_(bus), model_(model), router_(router),
      manager_(manager), graph_(graph) {
  // Telemetry counters exist in every mode (including Disabled) so callers can
  // unconditionally read .telemetry(). Disabled mode just never populates them.
  telemetry_ = std::make_unique<RuntimeTelemetry>();
  forensic::Log("HARNESS", std::string("ValidationHarness mode=") + ToString(mode_));

  if (mode_ == ValidationMode::Disabled) return;

  // Session directory + event-stream loggers.
  report_writer_      = std::make_unique<SessionReportWriter>();
  const std::string& dir = report_writer_->session_dir();
  event_trace_        = std::make_unique<ForensicLogger>(dir, "event_trace");
  projection_trace_   = std::make_unique<ForensicLogger>(dir, "projection_trace");
  tick_trace_         = std::make_unique<ForensicLogger>(dir, "tick_trace");
  integrity_trace_    = std::make_unique<ForensicLogger>(dir, "integrity_trace");

  // Always-on observers (PassiveTelemetry+).
  tick_monitor_       = std::make_unique<TickMonitor>(
      clock_, telemetry_.get(), tick_trace_.get());
  projection_auditor_ = std::make_unique<ProjectionAuditor>(
      bus_, router_, model_, telemetry_.get(), projection_trace_.get());
  integrity_auditor_  = std::make_unique<IntegrityAuditor>(
      clock_, bus_, manager_, model_, router_, graph_, telemetry_.get(),
      integrity_trace_.get());
  // PHASE 10.4B — semantic↔native activation/z coherency (kind-agnostic, log-only).
  coherency_auditor_  = std::make_unique<ActivationCoherencyAuditor>(
      clock_, bus_, model_);
  // M2.1C — ontology enforcement layer (constitutional §4c tripwire; §4b/firewall are structural).
  ontology_auditor_   = std::make_unique<OntologyAuditor>(bus_);

  InstallBusTap();

  // InteractiveStress+ exposes the simulator + scenario registry.
  if (mode_ == ValidationMode::InteractiveStress ||
      mode_ == ValidationMode::FullTorture) {
    simulator_  = std::make_unique<InteractionSimulator>(clock_, router_,
                                                         graph_);
    scenarios_  = std::make_unique<TestScenarios>(simulator_.get(), manager_);
    forensic::Log("HARNESS",
                  "scenarios registered: " +
                      std::to_string(scenarios_->Names().size()));
  }

  // FullTorture auto-runs scenarios after first frame.
  if (mode_ == ValidationMode::FullTorture) {
    ScheduleAutoTorture();
  }
}

ValidationHarness::~ValidationHarness() {
  // PHASE 8B-prep — pull all bus/clock subscriptions before the captured
  // pointers (event_trace_, telemetry_) go away.
  if (bus_ && bus_tap_token_ != 0) {
    bus_->Unsubscribe(bus_tap_token_);
    bus_tap_token_ = 0;
  }
  // Pull the auto-torture one-shot out of the clock if it never fired —
  // otherwise its callback (which captures `this`) lives on in the clock's
  // subscriber list with a dangling `this`.
  if (clock_ && auto_torture_token_ != 0) {
    clock_->Unsubscribe(auto_torture_token_);
    auto_torture_token_ = 0;
  }
  // Tear modules down in reverse construction order. Each module's destructor
  // unsubscribes from clock/bus, so the order matters: simulator before
  // tick_monitor before clock, etc.
  scenarios_.reset();
  soak_runner_.reset();
  simulator_.reset();
  integrity_auditor_.reset();
  projection_auditor_.reset();
  tick_monitor_.reset();
  integrity_trace_.reset();
  tick_trace_.reset();
  projection_trace_.reset();
  event_trace_.reset();
  report_writer_.reset();
}

bool ValidationHarness::RunScenario(const std::string& name) {
  if (!scenarios_) {
    forensic::Log("HARNESS",
                  "RunScenario('" + name + "') ignored — mode does not allow it");
    return false;
  }
  return scenarios_->Run(name);
}

void ValidationHarness::RunAllScenarios() {
  if (!scenarios_) return;
  // Sequentially: each scenario completes (simulator unsubscribes) before the
  // next one begins. The simulator's busy() guard rejects parallel runs.
  // Naive linear pump: each Run is async (drains over ticks), so to chain we
  // need to wait. For 7D we just queue them via the done-callback chain.
  std::vector<std::string> names = scenarios_->Names();
  forensic::Log("HARNESS",
                "RunAllScenarios queueing " + std::to_string(names.size()));
  // TODO 7E: scenarios chain via done-callback. For now the first runs and
  // the rest are no-ops while busy. The intentional minimal version.
  for (const auto& n : names) {
    if (!scenarios_->Run(n)) break;
  }
}

void ValidationHarness::FinalizeSession(const std::string& reason) {
  // Stop soak if running.
  soak_runner_.reset();
  if (report_writer_ && telemetry_) {
    report_writer_->WriteSummary(*telemetry_, ToString(mode_), reason);
  }
}

void ValidationHarness::StartSoak(double minutes) {
  if (!scenarios_ || !simulator_ || !clock_) {
    forensic::Log("HARNESS", "StartSoak: requires InteractiveStress+ mode");
    return;
  }
  soak_runner_ = std::make_unique<SoakRunner>(
      clock_, scenarios_.get(), simulator_.get(), manager_);
  soak_runner_->Start(minutes);
}

bool ValidationHarness::soak_running() const {
  return soak_runner_ && soak_runner_->running();
}

void ValidationHarness::InstallBusTap() {
  if (!bus_) return;
  ForensicLogger* event_trace = event_trace_.get();
  RuntimeTelemetry* t = telemetry_.get();

  bus_tap_token_ = bus_->Subscribe([t, event_trace](RuntimeEvent event, SurfaceShell* surface) {
    // Counter taps.
    switch (event) {
      case RuntimeEvent::SurfaceCreated:
        t->surfaces_created.fetch_add(1);
        t->surfaces_live.fetch_add(1);
        break;
      case RuntimeEvent::SurfaceDestroyed:
        t->surfaces_destroyed.fetch_add(1);
        t->surfaces_live.fetch_sub(1);
        break;
      case RuntimeEvent::InteractionBegan:
        t->interactions_started.fetch_add(1);
        t->capture_acquisitions.fetch_add(1);
        break;
      case RuntimeEvent::InteractionEnded:
        t->interactions_ended.fetch_add(1);
        t->capture_releases.fetch_add(1);
        break;
      case RuntimeEvent::InteractionUpdated:
        t->projections.fetch_add(1);
        break;
      default:
        break;
    }
    // Event trace: every event, with surface id when available.
    if (event_trace) {
      JsonLine line;
      line.Str("kind", "event")
          .Str("event", ToString(event))
          .Str("surface", surface ? surface->id() : "<none>");
      event_trace->LogEvent(line);
    }
  });
}

void ValidationHarness::ScheduleAutoTorture() {
  // Subscribe a member callback as a one-shot. Storing the token on the
  // harness means the destructor can yank the subscription if it hasn't fired
  // by then (guards against the lambda outliving the harness, which would
  // dereference a dangling `this` on the next tick).
  if (!clock_ || !scenarios_) return;
  auto_torture_token_ =
      clock_->Subscribe([this](double) { OnAutoTortureTick(); });
}

void ValidationHarness::OnAutoTortureTick() {
  if (auto_torture_fired_) return;
  auto_torture_fired_ = true;
  forensic::Log("HARNESS", "FullTorture: auto-running scenarios");
  RunAllScenarios();
  if (clock_ && auto_torture_token_ != 0) {
    const int t = auto_torture_token_;
    auto_torture_token_ = 0;  // clear BEFORE unsubscribe so destructor no-ops
    clock_->Unsubscribe(t);
  }
}
