#include "multisurface/orchestration_debugger.h"

#include <string>

#include "forensic_trace.h"
#include "interaction_router.h"
#include "multisurface/surface_graph.h"
#include "runtime_events.h"
#include "surface_shell.h"
#include "validation/forensic_logger.h"

namespace {
bool IsTopologyEvent(RuntimeEvent e) {
  return e == RuntimeEvent::SurfaceAttached ||
         e == RuntimeEvent::SurfaceDetached ||
         e == RuntimeEvent::SurfaceGrouped ||
         e == RuntimeEvent::SurfaceUngrouped ||
         e == RuntimeEvent::TopologyMutated ||
         e == RuntimeEvent::GroupFractured ||
         e == RuntimeEvent::SurfaceDocked ||
         e == RuntimeEvent::SurfaceUndocked;
}
// PHASE 8C/8D — fracture (extraction) AND dock events are EXPECTED runtime-driven
// topology mutation during interaction, NOT a sharp-edge WARN like an unrelated
// mid-drag regroup. SurfaceDocked rides the same TopologyMutated pre-state event,
// so treating dock like fracture keeps the WARN reserved for genuine surprises.
bool IsFractureEvent(RuntimeEvent e) {
  return e == RuntimeEvent::TopologyMutated ||
         e == RuntimeEvent::GroupFractured ||
         e == RuntimeEvent::SurfaceDocked;
}
}  // namespace

OrchestrationDebugger::OrchestrationDebugger(EventBus* bus, SurfaceGraph* graph,
                                             InteractionRouter* router,
                                             ForensicLogger* trace)
    : bus_(bus), graph_(graph), router_(router), trace_(trace) {
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent event, SurfaceShell* surface) {
          OnEvent(event, surface);
        });
  }
  forensic::Log("ORCHESTRATION", "OrchestrationDebugger installed");
}

OrchestrationDebugger::~OrchestrationDebugger() {
  if (bus_ && bus_token_ != 0) bus_->Unsubscribe(bus_token_);
}

void OrchestrationDebugger::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  if (!IsTopologyEvent(event)) return;

  const bool during_interaction =
      router_ && router_->mode() != InteractionMode::None;
  const std::string sid = surface ? surface->id() : "<none>";
  const std::string interacting_id =
      (router_ && router_->interacting()) ? router_->interacting()->id() : "";

  // PHASE 8C — fracture is the EXPECTED, runtime-driven extraction path. Log it
  // as a normal orchestration line (not a WARN) and return — the sharp-edge
  // warnings below are for UNEXPECTED mid-interaction topology edits.
  if (IsFractureEvent(event)) {
    forensic::Log("ORCHESTRATION",
                  std::string("fracture event=") + ToString(event) +
                      " surface=" + sid + " interacting=" + interacting_id);
    return;
  }

  // Sharp-edge case 1: topology mutated during an active interaction. Not
  // illegal, but it means whatever 8B/8C/8D do on the interaction's commit
  // path must account for a freshly-mutated graph. Surface this loudly.
  if (during_interaction) {
    forensic::Log("ORCHESTRATION WARN",
                  std::string("topology mutated during interaction: event=") +
                      ToString(event) + " surface=" + sid +
                      " interacting=" + interacting_id);
  }

  // Sharp-edge case 2: the dependent of an Attach/Detach IS the currently-
  // interacting surface. Geometry projection will continue but the new
  // relationship's constraints aren't applied yet (8B work).
  if (during_interaction && surface && router_->interacting() == surface &&
      (event == RuntimeEvent::SurfaceAttached ||
       event == RuntimeEvent::SurfaceDetached)) {
    forensic::Log("ORCHESTRATION WARN",
                  "interacting surface " + sid +
                      " just changed attachment — constraint enforcement is "
                      "deferred to 8B");
  }

  if (trace_) {
    JsonLine line;
    line.Str("kind", "topology")
        .Str("event", ToString(event))
        .Str("surface", sid)
        .Bool("during_interaction", during_interaction);
    if (graph_ && surface) {
      line.Int("group", static_cast<long long>(graph_->GroupOf(surface)))
          .Int("edge_count_total",
               static_cast<long long>(graph_->edge_count()));
      SurfaceShell* anchor = graph_->AnchorOf(surface);
      line.Str("anchor", anchor ? anchor->id() : "<none>");
    }
    trace_->LogEvent(line);
  }
}
