#include "validation/integrity_auditor.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "forensic_trace.h"
#include "interaction_router.h"
#include "multisurface/surface_graph.h"
#include "runtime_events.h"
#include "surface_manager.h"
#include "surface_model.h"
#include "surface_shell.h"
#include "validation/forensic_logger.h"
#include "validation/runtime_telemetry.h"

IntegrityAuditor::IntegrityAuditor(FrameClock* clock, EventBus* bus,
                                   SurfaceManager* manager, SurfaceModel* model,
                                   InteractionRouter* router,
                                   SurfaceGraph* graph,
                                   RuntimeTelemetry* telemetry,
                                   ForensicLogger* trace)
    : clock_(clock), bus_(bus), manager_(manager), model_(model),
      router_(router), graph_(graph), telemetry_(telemetry), trace_(trace) {
  if (clock_) {
    token_ = clock_->Subscribe([this](double dt) { OnTick(dt); });
  }
  if (bus_) {
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent event, SurfaceShell* surface) {
          OnEvent(event, surface);
        });
  }
  forensic::Log("AUDIT", "IntegrityAuditor installed");
}

IntegrityAuditor::~IntegrityAuditor() {
  if (clock_ && token_ != 0) clock_->Unsubscribe(token_);
  if (bus_ && bus_token_ != 0) bus_->Unsubscribe(bus_token_);
}

void IntegrityAuditor::OnTick(double /*dt*/) {
  ++tick_count_;
  // PHASE 8B-prep — transaction-leak watchdog (I-A3). If a transaction stays
  // open across event-pump returns while the router is idle, something
  // forgot to Commit. Catch within one tick (16ms) instead of waiting for
  // the 60-tick periodic audit. Cheap: two pointer dereferences.
  if (model_ && model_->InTransaction() && router_ &&
      router_->mode() == InteractionMode::None) {
    if (telemetry_) telemetry_->integrity_failures.fetch_add(1);
    forensic::Log("INTEGRITY FAIL",
                  "transaction leaked: model.InTransaction()=true but router "
                  "is Idle (no interaction owns this transaction)");
    if (trace_) {
      JsonLine line;
      line.Str("kind", "integrity_fail")
          .Str("reason", "txn-leak")
          .Str("detail", "transaction open with no active interaction");
      trace_->LogEvent(line);
    }
  }
  if (tick_count_ % kAuditEveryNTicks == 0) {
    RunAudit("periodic");
  }
}

void IntegrityAuditor::OnEvent(RuntimeEvent event, SurfaceShell* surface) {
  // Lifecycle events trigger immediate audit so dangling state is caught on
  // the exact tick it appears.
  if (event == RuntimeEvent::SurfaceCreated ||
      event == RuntimeEvent::SurfaceDestroyed) {
    RunAudit("lifecycle");
  }

  // PHASE 8D — I-D: a freshly docked SOURCE must now belong to a real group (the
  // dock just applied graph_->Group). It must NOT be ungrouped after a Docked
  // event. (The co-grouping of source+target is checked structurally by Group
  // itself, which rejects partial grouping; here we assert the source landed in a
  // group at all.)
  if (event == RuntimeEvent::SurfaceDocked && graph_ && surface) {
    if (graph_->GroupOf(surface) == SurfaceGraph::kNoGroup) {
      if (telemetry_) telemetry_->integrity_failures.fetch_add(1);
      forensic::Log("INTEGRITY FAIL",
                    "I-D: SurfaceDocked but source is ungrouped id=" +
                        surface->id());
    }
    // Also run the graph invariant suite so a corrupt merge is caught immediately.
    RunAudit("dock");
  }
}

void IntegrityAuditor::RunAudit(const char* reason) {
  if (!model_) return;
  if (telemetry_) telemetry_->integrity_checks.fetch_add(1);

  std::vector<std::string> failures;

  const auto& z = model_->z_order();
  SurfaceShell* active = model_->active();
  SurfaceShell* focused = model_->focused();

  // Build the membership set + check duplicates.
  std::unordered_set<SurfaceShell*> seen;
  for (SurfaceShell* s : z) {
    if (s == nullptr) {
      failures.push_back("z_order contains nullptr");
      continue;
    }
    if (!seen.insert(s).second) {
      failures.push_back("z_order duplicate id=" + s->id());
    }
    if (s->GetHandle() == nullptr) {
      failures.push_back("z_order entry has null HWND id=" + s->id());
    }
  }

  // active_ / focused_ membership.
  if (active && seen.find(active) == seen.end()) {
    failures.push_back("active_ not in z_order id=" + active->id());
  }
  if (focused && seen.find(focused) == seen.end()) {
    failures.push_back("focused_ not in z_order id=" + focused->id());
  }

  // bounds_ existence per z entry — read via the public bounds() (returns
  // GetWindowRect fallback if missing, which itself is a soft signal).
  for (SurfaceShell* s : z) {
    if (!s) continue;
    RECT b = model_->bounds(s);
    if (b.right - b.left <= 0 || b.bottom - b.top <= 0) {
      failures.push_back("invalid bounds id=" + s->id());
    }
  }

  // PHASE 8A.2 — cross-system invariants. Router state ↔ model state ↔ OS
  // capture state must agree at all times. Catching a divergence here means
  // a lifecycle path forgot to balance one of them.
  if (router_) {
    const InteractionMode mode = router_->mode();
    SurfaceShell* interacting = router_->interacting();
    const bool subscribed = router_->subscribed_to_clock();
    const bool in_txn = model_->InTransaction();
    HWND captured = GetCapture();

    if (mode == InteractionMode::None) {
      // I-R1: when idle, interacting_ must be null.
      if (interacting != nullptr) {
        failures.push_back("router idle but interacting_!=null id=" +
                            interacting->id());
      }
      // I-R2: when idle, no clock subscription (lazy-idle property).
      if (subscribed) {
        failures.push_back("router idle but still subscribed to clock");
      }
      // I-R3: when idle, no open transaction.
      if (in_txn) {
        failures.push_back("router idle but model.InTransaction()=true");
      }
    } else {
      // I-R4: when interacting, interacting_ must be non-null and in z_order.
      if (interacting == nullptr) {
        failures.push_back("router mode!=None but interacting_=null");
      } else if (seen.find(interacting) == seen.end()) {
        failures.push_back("router interacting_ not in z_order id=" +
                            interacting->id());
      }
      // I-R5: when interacting, clock subscription must be active.
      if (!subscribed) {
        failures.push_back("router interacting but NOT subscribed to clock");
      }
      // I-R6: when interacting, model transaction must be open.
      if (!in_txn) {
        failures.push_back("router interacting but model NOT in transaction");
      }
      // I-R7: when interacting, the OS must agree we hold capture.
      if (interacting && captured != interacting->GetHandle()) {
        failures.push_back("router interacting but capture HWND mismatch "
                            "(OS says other window owns capture)");
      }
      // PHASE 8C — I-X2: capture NEVER transfers during a fracture. Even with
      // derived (extracted) sessions present, the ONE capture owner stays the
      // primary leader's HWND — derived sessions own no capture. (I-R7 already
      // asserts capture == leader; this comment documents that it must hold
      // unchanged even while derived_count() > 0, which is the 8C guarantee.)
    }
  }

  // PHASE 8B.5 — topology invariants from SurfaceGraph::CheckInvariants.
  // Chaos scenarios that mutate the graph during interactions can corrupt
  // group_of_ ↔ groups_ consistency or leak singleton groups. These get
  // labeled with their T#-tag so the cause is immediately attributable.
  if (graph_) {
    for (const std::string& t : graph_->CheckInvariants()) {
      failures.push_back("graph: " + t);
    }
  }

  if (!failures.empty()) {
    if (telemetry_) telemetry_->integrity_failures.fetch_add(static_cast<int>(failures.size()));
    for (const auto& f : failures) {
      forensic::Log("INTEGRITY FAIL", std::string(reason) + ": " + f);
      if (trace_) {
        JsonLine line;
        line.Str("kind", "integrity_fail").Str("reason", reason).Str("detail", f);
        trace_->LogEvent(line);
      }
    }
  } else if (trace_) {
    JsonLine line;
    line.Str("kind", "integrity_ok")
        .Str("reason", reason)
        .Int("z_count", static_cast<long long>(z.size()))
        .Str("active", active ? active->id() : "<none>")
        .Str("focused", focused ? focused->id() : "<none>");
    trace_->LogEvent(line);
  }
}
