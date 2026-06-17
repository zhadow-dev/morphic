#include "validation/soak_runner.h"

#include <cstdio>
#include <string>

#include "forensic_trace.h"
#include "surface_manager.h"
#include "validation/interaction_simulator.h"
#include "validation/test_scenarios.h"

namespace {

double ElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) {
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  if (freq.QuadPart == 0) return 0.0;
  return (end.QuadPart - start.QuadPart) * 1000.0 /
         static_cast<double>(freq.QuadPart);
}

}  // namespace

SoakRunner::SoakRunner(FrameClock* clock, TestScenarios* scenarios,
                       InteractionSimulator* simulator,
                       SurfaceManager* manager)
    : clock_(clock), scenarios_(scenarios), simulator_(simulator),
      manager_(manager) {}

SoakRunner::~SoakRunner() {
  if (clock_ && token_ != 0) {
    clock_->Unsubscribe(token_);
    token_ = 0;
  }
}

void SoakRunner::Start(double duration_minutes) {
  if (running_ || !clock_ || !scenarios_) return;

  scenario_names_ = scenarios_->Names();
  if (scenario_names_.empty()) {
    forensic::Log("SOAK", "No scenarios registered — cannot start soak");
    return;
  }

  // Filter out scenarios that destroy surfaces — in a long soak, we need
  // surfaces to stay alive for the full duration. Block ANY scenario that
  // contains 'destroy' (catches destroy_leader, destroy_follower_mid_drag, etc.)
  // or 'lifecycle_torture' (rapid create/destroy churn).
  std::vector<std::string> safe;
  for (const auto& name : scenario_names_) {
    if (name.find("destroy") != std::string::npos) continue;
    if (name.find("lifecycle") != std::string::npos) continue;
    safe.push_back(name);
  }
  scenario_names_ = std::move(safe);

  if (scenario_names_.empty()) {
    forensic::Log("SOAK", "All scenarios filtered — cannot start soak");
    return;
  }

  duration_limit_ms_ = duration_minutes * 60.0 * 1000.0;
  current_index_ = 0;
  scenarios_completed_ = 0;
  cycles_completed_ = 0;
  cooldown_ticks_ = 0;
  running_ = true;

  QueryPerformanceCounter(&start_qpc_);
  last_qpc_ = start_qpc_;

  char buf[128];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "Soak started: %.0f min, %zu scenarios per cycle",
              duration_minutes, scenario_names_.size());
  forensic::Log("SOAK", buf);

  // Subscribe to clock and fire the first scenario.
  token_ = clock_->Subscribe([this](double dt_ms) { OnTick(dt_ms); });
  RunNext();
}

double SoakRunner::elapsed_minutes() const {
  if (start_qpc_.QuadPart == 0) return 0.0;
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  return ElapsedMs(start_qpc_, now) / 60000.0;
}

void SoakRunner::OnTick(double /*dt_ms*/) {
  if (!running_) return;

  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);

  // Check duration limit.
  const double elapsed_ms = ElapsedMs(start_qpc_, now);
  if (elapsed_ms >= duration_limit_ms_) {
    Finish();
    return;
  }

  // Periodic progress log every 5 minutes.
  const double since_last = ElapsedMs(last_qpc_, now);
  if (since_last > 5.0 * 60.0 * 1000.0) {
    last_qpc_ = now;
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "Soak progress: %.1f min elapsed, %d scenarios done, "
                "%d cycles done",
                elapsed_ms / 60000.0, scenarios_completed_, cycles_completed_);
    forensic::Log("SOAK", buf);
  }

  // Cooldown between scenarios — let the runtime settle.
  if (cooldown_ticks_ > 0) {
    --cooldown_ticks_;
    if (cooldown_ticks_ == 0) {
      RunNext();
    }
    return;
  }

  // Poll simulator completion.
  if (waiting_for_completion_ && simulator_ && !simulator_->busy()) {
    waiting_for_completion_ = false;
    ++scenarios_completed_;
    const std::string& completed_name = scenario_names_[
        (current_index_ == 0 ? scenario_names_.size() : current_index_) - 1];
    forensic::Log("SOAK", "Scenario '" + completed_name + "' done (" +
                              std::to_string(scenarios_completed_) + " total)");

    if (current_index_ == 0) {
      ++cycles_completed_;
      forensic::Log("SOAK", "Cycle " + std::to_string(cycles_completed_) +
                                " completed");
    }
    cooldown_ticks_ = kCooldownTicks;
  }
}

void SoakRunner::RunNext() {
  if (!running_ || !scenarios_) return;

  const std::string& name = scenario_names_[current_index_];
  bool ok = scenarios_->Run(name);
  if (!ok) {
    // Scenario rejected (busy or missing). Try next after cooldown.
    forensic::Log("SOAK", "Scenario '" + name + "' rejected — skip to next");
    current_index_ = (current_index_ + 1) % scenario_names_.size();
    cooldown_ticks_ = kCooldownTicks;
    return;
  }
  // Scenario is now running asynchronously via clock ticks.
  // Advance index for next time and set waiting flag.
  current_index_ = (current_index_ + 1) % scenario_names_.size();
  waiting_for_completion_ = true;
}



void SoakRunner::Finish() {
  running_ = false;
  if (clock_ && token_ != 0) {
    const int t = token_;
    token_ = 0;
    clock_->Unsubscribe(t);
  }

  char buf[256];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "Soak COMPLETE: %.1f min, %d scenarios, %d cycles",
              elapsed_minutes(), scenarios_completed_, cycles_completed_);
  forensic::Log("SOAK", buf);
}
