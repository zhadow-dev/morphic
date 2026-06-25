#ifndef RUNNER_MORPHIC_RUNTIME_H_
#define RUNNER_MORPHIC_RUNTIME_H_

#include <flutter/dart_project.h>

#include <memory>

#include "surface_manager.h"

class CompositorRuntime;
class EventBus;
class FaultInjector;
class FrameClock;
class InteractionRouter;
class OrchestrationDebugger;
class PresentationCoordinator;
class SurfaceGraph;
class SurfaceModel;
class SurfaceLifecycleRelay;
class ValidationHarness;
namespace morphic::policy { class SurfaceEcology; }
namespace morphic { class ChurnHarness; }
namespace morphic::scene { class SceneMirror; }
#if defined(MORPHIC_SPATIAL)
namespace morphic::ng { class ProjectionReconciler; }
namespace morphic::ng { class SpatialCompositor; }
#endif
namespace morphic::workspace { class CompositionProjector; }
namespace morphic::workspace { class PlaneActivationModel; }
namespace morphic::workspace { class PlaneVisualProjector; }
namespace morphic::workspace { class RelationshipGravityController; }
namespace morphic::workspace { class SceneZoomController; }

// PHASE 2 / stabilization — MorphicRuntime
//
// The runtime lifecycle coordinator. It owns the base DartProject, the event
// bus, the interaction router, and the SurfaceManager, wires them together, and
// owns app-lifetime policy (quit-on-empty — moved here from the manager per
// CORE_HARDENING F8).
class MorphicRuntime {
 public:
  explicit MorphicRuntime(const flutter::DartProject& project);
  ~MorphicRuntime();

  MorphicRuntime(const MorphicRuntime&) = delete;
  MorphicRuntime& operator=(const MorphicRuntime&) = delete;

  bool Create();
  void Destroy();

  // PHASE 8B.7 — the runtime owns the FrameClock; the epoch-mode FrameScheduler
  // (constructed in wWinMain) needs to pump it. Returns nullptr before Create().
  // Ownership stays with the runtime — the scheduler holds this as a raw pointer
  // and is torn down (stack scope in wWinMain) before MorphicRuntime::Destroy.
  FrameClock* clock() const { return clock_.get(); }

 private:
  flutter::DartProject project_;
  // Declaration order matters for teardown: members destruct in reverse.
  // Reverse order: manager_ first (references router/model/bus), then router_
  // (subscribes to clock_ and references model_/bus_), then clock_ (must
  // outlive router so any in-flight subscription's lambda capture stays valid
  // until the router unsubscribes), then model_, then bus_.
  std::unique_ptr<EventBus> bus_;
  std::unique_ptr<SurfaceModel> model_;
  std::unique_ptr<FrameClock> clock_;
  // PHASE 8B — graph BEFORE router so the router can take graph_.get() at
  // construction and so reverse-declaration-order destruction destroys
  // router_ before graph_ (router holds a raw graph_ pointer).
  std::unique_ptr<SurfaceGraph> graph_;
  // PHASE 9 — CompositorRuntime (projection seam). Declared after model_/clock_
  // (both outlive it; it reads neither in its ctor). model_ + coordinator_ hold a
  // raw compositor_ pointer; reverse-declaration destruction destroys
  // router_/coordinator_ first, then compositor_, while model_ (declared earlier)
  // still exists — model_'s dtor never projects, so order is safe.
  std::unique_ptr<CompositorRuntime> compositor_;
  // PHASE 8E — PresentationCoordinator BEFORE router so the router can take
  // coordinator_.get() at construction; reverse-destruction destroys router_
  // before coordinator_ (router holds a raw coordinator_ pointer). It holds raw
  // model_+clock_ (declared earlier → outlive it). Settle-only presentation layer.
  std::unique_ptr<PresentationCoordinator> coordinator_;
  std::unique_ptr<InteractionRouter> router_;
  std::unique_ptr<OrchestrationDebugger> orchestration_debugger_;
  // PHASE 7D — ValidationHarness orchestrates all observability subsystems
  // (telemetry, structured forensic logs, tick monitor, projection auditor,
  // integrity auditor, simulator, scenarios, session report). Mode is selected
  // by kValidationMode in morphic_runtime.cpp. Faults remain a SEPARATE chaos
  // system (orthogonal to validation observability).
  std::unique_ptr<ValidationHarness> harness_;
  std::unique_ptr<FaultInjector> faults_;
  std::unique_ptr<SurfaceManager> manager_;
  // PHASE 10.1 — surface ecology (product policy layer). Declared AFTER manager_
  // and bus_ (holds raw pointers to both); reverse-destruction destroys ecology_
  // first, while manager_/bus_ still exist (its registry unsubscribes from bus_).
  std::unique_ptr<morphic::policy::SurfaceEcology> ecology_;
  // PHASE 11 — churn/leak stress harness (gated by kChurnHarness). Declared
  // after ecology_ (drives it); ticked from the frame clock.
  std::unique_ptr<morphic::ChurnHarness> churn_;
  // MORPHIC NG Stage 1 — the retained scene graph as an OBSERVER of ecology truth
  // (no authority; see scene/scene_mirror.h). Declared after ecology_ (reads it);
  // reverse-destruction tears the mirror down first.
  std::unique_ptr<morphic::scene::SceneMirror> scene_mirror_;
  // MORPHIC NG Stage 5 — hybrid backend orchestrator (parallel subsystem beside
  // the frozen runtime). Adopts backend=spatial surfaces into composited GPU
  // visuals; reads ecology + manager, subscribes to bus_. Reset before ecology_.
  // MORPHIC NG Phase 1 — the ProjectionTruth seam's product half (visual-follow
  // + parked-engine translation). Declared before spatial_compositor_ (which
  // registers surfaces into it); the interceptor installed on compositor_ is
  // cleared in Destroy() before this dies.
#if defined(MORPHIC_SPATIAL)
  std::unique_ptr<morphic::ng::ProjectionReconciler> projection_reconciler_;
  std::unique_ptr<morphic::ng::SpatialCompositor> spatial_compositor_;
#endif
  // PHASE 12D — shared-drag movement projector (reads ecology's composition graph; owns
  // no runtime state). Declared after ecology_; Destroy() also resets it explicitly
  // before ecology_ so the read-dependency is safe regardless of destruction path.
  std::unique_ptr<morphic::workspace::CompositionProjector> composition_projector_;
  // SPATIAL MIGRATION Stage 1 — plane activation model (product layer; reads ecology's
  // composition graph, observes the bus, projects nothing). Same destruction discipline as
  // composition_projector_: reset BEFORE ecology_ and while bus_ still exists.
  std::unique_ptr<morphic::workspace::PlaneActivationModel> plane_activation_;
  // SPATIAL MIGRATION Stage 2A — content-level plane visual projector. Declared AFTER
  // plane_activation_ (registers a callback on it) so reverse-destruction destroys the
  // projector FIRST (it detaches the callback in its dtor). Holds raw model_+manager_.
  std::unique_ptr<morphic::workspace::PlaneVisualProjector> plane_visual_;
  // M2.3G — relationship gravity (product layer; reads ecology's composition graph + model_/manager_).
  // Reset before ecology_ (read-dependency), like the projectors above.
  std::unique_ptr<morphic::workspace::RelationshipGravityController> gravity_;
  // M2.8.3 — scene zoom (product layer; sibling of gravity). One-shot composition reprojection (the
  // camera-move replacement for maximize). Reset before ecology_ (reads its composition graph).
  std::unique_ptr<morphic::workspace::SceneZoomController> scene_zoom_;
  // M2.1D — projects generic surface lifecycle/identity facts onto the app bus (subscribes bus_,
  // enumerates manager_). Declared after manager_/bus_ (both outlive it); unsubscribes in dtor.
  std::unique_ptr<SurfaceLifecycleRelay> lifecycle_relay_;
  int soak_start_token_ = 0;  // PHASE 8B.7 — one-shot clock token for delayed soak start
};

#endif  // RUNNER_MORPHIC_RUNTIME_H_
