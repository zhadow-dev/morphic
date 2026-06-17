#ifndef RUNNER_MULTISURFACE_ORCHESTRATION_DEBUGGER_H_
#define RUNNER_MULTISURFACE_ORCHESTRATION_DEBUGGER_H_

#include "runtime_events.h"

class EventBus;
class ForensicLogger;
class InteractionRouter;
class SurfaceGraph;
class SurfaceShell;

// PHASE 8A — OrchestrationDebugger
//
// Passive observer that listens to topology mutations + interaction lifecycle
// and writes structured NDJSON to orchestration_trace (if a logger is wired)
// AND flags sharp-edge cases to the forensic log:
//   - topology mutation during an active interaction
//   - attach/detach of a dependent that is the active interaction surface
//   - group of a surface whose anchor is being detached in the same epoch
//
// Sister to InteractionAudit (target/presented/native check) but for the
// semantic topology axis. Always-on cost: one event-bus subscriber.
class OrchestrationDebugger {
 public:
  OrchestrationDebugger(EventBus* bus, SurfaceGraph* graph,
                        InteractionRouter* router,
                        ForensicLogger* trace);
  ~OrchestrationDebugger();

  OrchestrationDebugger(const OrchestrationDebugger&) = delete;
  OrchestrationDebugger& operator=(const OrchestrationDebugger&) = delete;

 private:
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);

  EventBus* bus_;
  SurfaceGraph* graph_;
  InteractionRouter* router_;
  ForensicLogger* trace_;
  int bus_token_ = 0;  // I-A2 closure
};

#endif  // RUNNER_MULTISURFACE_ORCHESTRATION_DEBUGGER_H_
