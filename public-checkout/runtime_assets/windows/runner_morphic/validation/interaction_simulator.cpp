#include "validation/interaction_simulator.h"

#include <string>
#include <utility>

#include "forensic_trace.h"
#include "interaction_router.h"
#include "multisurface/surface_graph.h"
#include "surface_shell.h"

InteractionSimulator::InteractionSimulator(FrameClock* clock,
                                           InteractionRouter* router,
                                           SurfaceGraph* graph)
    : clock_(clock), router_(router), graph_(graph) {}

InteractionSimulator::~InteractionSimulator() {
  if (clock_ && token_ != 0) clock_->Unsubscribe(token_);
}

bool InteractionSimulator::Run(const std::string& scenario_name,
                               SurfaceShell* surface, std::vector<Step> steps,
                               DoneCallback done) {
  if (!script_.empty()) {
    forensic::Log("SIM",
                  "Run rejected — another scenario in flight ('" +
                      scenario_name_ + "')");
    return false;
  }
  if (!clock_ || !router_ || !surface) return false;
  script_ = std::move(steps);
  cursor_ = 0;
  wait_remaining_ = 0;
  target_surface_ = surface;
  scenario_name_ = scenario_name;
  done_ = std::move(done);

  // Subscribe lazily for the duration of the script.
  token_ = clock_->Subscribe([this](double dt) { OnTick(dt); });
  forensic::Log("SIM", "Run start scenario=" + scenario_name_ +
                           " steps=" + std::to_string(script_.size()) +
                           " surface=" + target_surface_->id());
  return true;
}

void InteractionSimulator::OnTick(double /*dt_ms*/) {
  if (script_.empty()) return;
  if (wait_remaining_ > 0) {
    --wait_remaining_;
    return;
  }
  if (cursor_ >= script_.size()) {
    // Script drained — clean up.
    const std::string name = scenario_name_;
    DoneCallback done = std::move(done_);
    script_.clear();
    cursor_ = 0;
    target_surface_ = nullptr;
    scenario_name_.clear();
    if (clock_ && token_ != 0) {
      clock_->Unsubscribe(token_);
      token_ = 0;
    }
    forensic::Log("SIM", "Run done scenario=" + name);
    if (done) done(name);
    return;
  }

  const Step& step = script_[cursor_];
  ApplyStep(step);
  if (step.op == Op::Wait) {
    wait_remaining_ = step.ticks > 0 ? step.ticks - 1 : 0;
  }
  ++cursor_;
}

void InteractionSimulator::ApplyStep(const Step& step) {
  if (!router_ || !target_surface_) return;
  switch (step.op) {
    case Op::Begin:
      forensic::Log("SIM", "step=Begin pt=(" +
                                std::to_string(step.pt.x) + "," +
                                std::to_string(step.pt.y) + ")");
      router_->BeginDrag(target_surface_, step.pt);
      break;
    case Op::BeginResize:
      forensic::Log("SIM", "step=BeginResize edge=" +
                                std::to_string(step.resize_edge) +
                                " pt=(" + std::to_string(step.pt.x) + "," +
                                std::to_string(step.pt.y) + ")");
      router_->BeginResize(target_surface_, step.pt, step.resize_edge);
      break;
    case Op::Update:
      router_->UpdatePointer(step.pt);
      break;
    case Op::End:
      forensic::Log("SIM", "step=End");
      router_->EndInteraction();
      break;
    case Op::Cancel:
      forensic::Log("SIM", "step=Cancel");
      router_->CancelInteraction(target_surface_, "simulator");
      break;
    case Op::Wait:
      forensic::Log("SIM",
                    "step=Wait ticks=" + std::to_string(step.ticks));
      break;
    case Op::DestroySurface: {
      // PHASE 8B.5 — mid-script destruction. If target is null, destroy the
      // primary target_surface_ (leader); otherwise destroy step.target
      // (typically a follower). After destruction we don't reuse the pointer
      // for subsequent steps — chaos scenarios must order steps accordingly.
      SurfaceShell* victim = step.target ? step.target : target_surface_;
      if (!victim || !victim->GetHandle()) {
        forensic::Log("SIM",
                      "step=DestroySurface — victim already dead, no-op");
        break;
      }
      const std::string vid = victim->id();
      forensic::Log("SIM", "step=DestroySurface id=" + vid);
      DestroyWindow(victim->GetHandle());
      // If we just destroyed our drive target, null it so subsequent input
      // steps no-op rather than crash. Group-member case is unaffected.
      if (victim == target_surface_) target_surface_ = nullptr;
      break;
    }
    case Op::StealCapture: {
      // PHASE 8B.5 — synthesize external capture loss. ReleaseCapture()
      // dispatches WM_CAPTURECHANGED synchronously to whatever window held
      // capture; the router's CancelInteraction path runs.
      forensic::Log("SIM", "step=StealCapture (ReleaseCapture)");
      ReleaseCapture();
      break;
    }
    case Op::GroupSurfaces: {
      if (!graph_) {
        forensic::Log("SIM", "step=GroupSurfaces — no graph wired, no-op");
        break;
      }
      if (!target_surface_ || !step.target) {
        forensic::Log("SIM",
                      "step=GroupSurfaces — missing operand, no-op");
        break;
      }
      const SurfaceGraph::GroupId gid =
          graph_->Group({target_surface_, step.target});
      forensic::Log("SIM", "step=GroupSurfaces (" + target_surface_->id() +
                              ", " + step.target->id() +
                              ") -> group=" + std::to_string(gid));
      break;
    }
    case Op::UngroupSurface: {
      if (!graph_) {
        forensic::Log("SIM", "step=UngroupSurface — no graph wired, no-op");
        break;
      }
      SurfaceShell* victim = step.target ? step.target : target_surface_;
      if (!victim) {
        forensic::Log("SIM",
                      "step=UngroupSurface — no target, no-op");
        break;
      }
      forensic::Log("SIM", "step=UngroupSurface id=" + victim->id());
      graph_->Ungroup(victim);
      break;
    }
  }
}
