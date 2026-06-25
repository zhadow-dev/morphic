#ifndef RUNNER_INTERACTION_ROUTER_H_
#define RUNNER_INTERACTION_ROUTER_H_

#include <windows.h>

#include <memory>
#include <optional>

#include "multisurface/docking_model.h"        // DockingThresholds
#include "multisurface/extraction_model.h"     // ExtractionThresholds
#include "multisurface/interaction_session.h"  // InteractionMode + Velocity + Metrics

class EventBus;
class FrameClock;
class InteractionEpoch;
class SurfaceGraph;
class SurfaceModel;
class SurfaceShell;
class PresentationCoordinator;

// PHASE 8B — InteractionRouter
//
// Front door for interactions. Owns the Win32 capture lifecycle and the
// activation path. Creates an InteractionSession on Begin and routes input
// to it; the session owns everything else (geometry, clock subscription,
// transaction, projection, metrics, lifecycle events).
//
// The router's only durable state during an interaction is the
// `std::unique_ptr<InteractionSession> session_`. Public queries (mode,
// interacting, target_bounds, etc.) forward to the session when present, or
// return defaults (None / nullptr / nullopt) when idle. The validation
// infrastructure (IntegrityAuditor, ProjectionAuditor) reads through these
// getters and does not need to know whether a session is multi-member.
//
// Single-surface case: session has 1 member (the leader). N-member case:
// session has all group members from the SurfaceGraph. Resize sessions are
// always 1-member regardless of group membership (group resize semantics
// is 8C+ work).
class InteractionRouter {
 public:
  using Metrics = InteractionSession::Metrics;
  using Velocity = InteractionSession::Velocity;

  InteractionRouter(SurfaceModel* model, EventBus* bus, FrameClock* clock,
                    SurfaceGraph* graph, PresentationCoordinator* coordinator);
  ~InteractionRouter();

  InteractionRouter(const InteractionRouter&) = delete;
  InteractionRouter& operator=(const InteractionRouter&) = delete;

  // THE single activation path. Every Win32 activation message normalizes here.
  void RequestActivate(SurfaceShell* surface);

  // Creates a new InteractionSession. For Dragging, expands to include all
  // group members from the SurfaceGraph (if any). For Resizing, always
  // single-member. Sets capture on the leader's HWND.
  void BeginDrag(SurfaceShell* surface, POINT pointer_screen);
  void BeginResize(SurfaceShell* surface, POINT pointer_screen, int resize_edge);

  void UpdatePointer(POINT pointer_screen);

  // Clean release path: session snaps to target, projects, commits, publishes.
  void EndInteraction();
  // Reason makes the trace unambiguous (capture-changed / destroy / simulator
  // / etc.).
  void CancelInteraction(SurfaceShell* surface, const char* reason);

  // Forwards to session.OnDpiChangedLeader if the session's leader matches.
  void OnDpiChanged(SurfaceShell* surface, const RECT& new_bounds);

  // Public queries — all forward to the epoch's primary session when present.
  InteractionMode mode() const;
  SurfaceShell* interacting() const;
  bool subscribed_to_clock() const;
  RECT target_bounds() const;
  std::optional<RECT> presented_bounds() const;
  Velocity current_velocity() const;
  const Metrics& last_metrics() const { return last_metrics_; }
  // PHASE 8C — number of derived (extracted) sessions in the active epoch (0 idle).
  size_t derived_count() const;

 private:
  void DestroyEpoch();  // unique_ptr reset + copy metrics into last_metrics_
  // PHASE 8C — the epoch's fracture observer: PRE-state event → graph mutation →
  // tether → POST-state events (the epoch spawns the derived session itself).
  void OnFracture(SurfaceShell* member, const morphic::ExtractionIntent& intent);
  // PHASE 8D — dock observer: publishes TopologyMutated (pre) → graph Group →
  // SurfaceDocked (post). Does NOT restructure the live session or transfer
  // capture; the rigid co-move starts on the next drag.
  void OnDock(SurfaceShell* source, SurfaceShell* target);

  SurfaceModel* model_;
  EventBus* bus_;
  FrameClock* clock_;
  SurfaceGraph* graph_;
  PresentationCoordinator* coordinator_;  // PHASE 8E — settle projection (not owned)

  // PHASE 8C — one InteractionEpoch per pointer interaction (owns the single
  // transaction + clock subscription + the primary & derived sessions).
  std::unique_ptr<InteractionEpoch> epoch_;
  morphic::ExtractionThresholds thresholds_{};  // tunable; defaults are conservative
  morphic::DockingThresholds dock_thresholds_{};  // PHASE 8D — tunable; conservative
  Metrics last_metrics_{};
  // Default values returned by query getters when no epoch is active.
  RECT empty_rect_{};
};

#endif  // RUNNER_INTERACTION_ROUTER_H_
