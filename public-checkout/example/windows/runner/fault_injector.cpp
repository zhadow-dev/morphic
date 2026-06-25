#include "fault_injector.h"

#include <windows.h>

#include <string>

#include "forensic_trace.h"
#include "interaction_router.h"
#include "surface_model.h"
#include "surface_shell.h"

FaultInjector::FaultInjector(FrameClock* clock, EventBus* bus,
                             SurfaceModel* model, InteractionRouter* router)
    : clock_(clock), bus_(bus), model_(model), router_(router) {}

FaultInjector::~FaultInjector() { RemoveAll(); }

void FaultInjector::InstallDummySubscribers(int count) {
  if (clock_ == nullptr || count <= 0) return;
  for (int i = 0; i < count; ++i) {
    FrameClock::Token token = clock_->Subscribe([](double) {
      // intentionally empty — exists to populate the subscriber set
    });
    clock_tokens_.push_back(token);
  }
  forensic::Log("FAULT", "InstallDummySubscribers count=" +
                             std::to_string(count) +
                             " total_tokens=" +
                             std::to_string(clock_tokens_.size()));
}

void FaultInjector::InstallSlowSubscriber(int sleep_ms) {
  if (clock_ == nullptr || sleep_ms <= 0) return;
  FrameClock::Token token = clock_->Subscribe([sleep_ms](double dt) {
    // Deliberately block the tick dispatch for `sleep_ms`. The clock has no
    // backpressure (see 7C limitations); this should still degrade smoothly:
    // peak_tick_interval rises; no recursive tick bursts; no missed
    // unsubscribes; router still gets dt with the elapsed wall time.
    forensic::Log("FAULT", "slow tick start dt=" + std::to_string(dt) +
                               "ms sleep=" + std::to_string(sleep_ms) + "ms");
    Sleep(static_cast<DWORD>(sleep_ms));
  });
  clock_tokens_.push_back(token);
  forensic::Log("FAULT",
                "InstallSlowSubscriber sleep_ms=" + std::to_string(sleep_ms) +
                    " token=" + std::to_string(token));
}

void FaultInjector::InstallReentrancyHook() {
  if (model_ == nullptr || router_ == nullptr || reentrancy_installed_) return;
  // Captured pointers: stable for the runtime lifetime (FaultInjector and the
  // model/router all destruct together when the runtime tears down).
  SurfaceModel* model = model_;
  InteractionRouter* router = router_;
  model_->SetTestProjectionCallback([model, router]() {
    // We are inside SurfaceModel::SetBounds, between projecting_=true and the
    // SetWindowPos that follows. Calling SetBounds again here MUST hit the R4
    // guard and increment reentrant_drops_, not recurse and not corrupt state.
    SurfaceShell* s = router->interacting();
    if (s == nullptr) return;
    forensic::Log("FAULT", "reentrant SetBounds attempt id=" + s->id());
    model->SetBounds(s, router->target_bounds());
  });
  reentrancy_installed_ = true;
  forensic::Log("FAULT", "InstallReentrancyHook (model test callback armed)");
}

void FaultInjector::RemoveAll() {
  for (FrameClock::Token t : clock_tokens_) {
    if (clock_) clock_->Unsubscribe(t);
  }
  if (!clock_tokens_.empty()) {
    forensic::Log("FAULT", "RemoveAll: unsubscribed " +
                               std::to_string(clock_tokens_.size()) +
                               " clock token(s)");
  }
  clock_tokens_.clear();
  if (reentrancy_installed_ && model_) {
    model_->SetTestProjectionCallback({});
    reentrancy_installed_ = false;
    forensic::Log("FAULT", "RemoveAll: disarmed model test callback");
  }
}
