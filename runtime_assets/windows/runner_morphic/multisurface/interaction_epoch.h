#ifndef RUNNER_MULTISURFACE_INTERACTION_EPOCH_H_
#define RUNNER_MULTISURFACE_INTERACTION_EPOCH_H_

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "frame_clock.h"
#include "multisurface/docking_model.h"
#include "multisurface/extraction_model.h"
#include "multisurface/interaction_session.h"

class EventBus;
class SurfaceModel;
class SurfaceGraph;
class SurfaceShell;
class PresentationCoordinator;

// PHASE 8C — InteractionEpoch.
//
// ONE pointer interaction = ONE transactional epoch, even after topology fracture.
// The epoch is the SHARED-concern owner so that a fracture does NOT create a
// second transaction / second clock subscription (which would cause staggered
// reconciliation + replay nondeterminism). It owns:
//
//   - the single SurfaceModel transaction      (Begin at ctor, Commit ONCE at End)
//   - the single FrameClock subscription        (its tick drives every session)
//   - the pending-fracture queue                (applied at the tick boundary)
//   - the lifetime of the primary + derived InteractionSessions
//
// SACRED LAWS:
//   - Topology mutation applies ONLY at a tick boundary, never mid-projection.
//   - The epoch EXCLUSIVELY owns session lifetime: sessions never spawn/destroy
//     sessions or touch the derived list — only the epoch does, at the boundary.
//   - Capture never transfers; the original leader's HWND owns OS capture for the
//     whole epoch (the router does SetCapture/ReleaseCapture, not the epoch).
//   - Scope: interaction transactional lifetime ONLY. NOT a scheduler/renderer/
//     graph manager. (Unrelated to 8B.7 FrameEpoch = one scheduler tick.)
//
// Single fracture depth: a derived session never extracts (the session disables
// detection when derived).
class InteractionEpoch {
 public:
  // Called when the epoch applies a fracture at the tick boundary. The router
  // wires this to: publish TopologyMutated (pre-state) → graph Ungroup → set
  // ExtractionTether → (epoch spawns the derived session) → publish
  // GroupFractured/SurfaceDetached. Returns nothing; the epoch handles the
  // derived-session spawn itself (it owns session lifetime).
  using FractureObserver =
      std::function<void(SurfaceShell* member, const morphic::ExtractionIntent&)>;

  // PHASE 8D — called when the epoch applies a DOCK at the tick boundary. The
  // router wires this to: publish TopologyMutated (pre-state, payload=source) →
  // graph_->Group({source, target}) → publish SurfaceDocked (post). The epoch does
  // NOT restructure the live session — both surfaces stay independent; the group
  // is topology truth and the rigid co-move begins on the NEXT drag.
  using DockObserver =
      std::function<void(SurfaceShell* source, SurfaceShell* target)>;

  InteractionEpoch(SurfaceModel* model, FrameClock* clock, EventBus* bus,
                   SurfaceGraph* graph, PresentationCoordinator* coordinator,
                   InteractionMode mode, SurfaceShell* leader,
                   std::vector<SurfaceShell*> members, POINT pointer_origin,
                   int resize_edge,
                   const morphic::ExtractionThresholds& thresholds,
                   const morphic::DockingThresholds& dock_thresholds,
                   FractureObserver on_fracture, DockObserver on_dock);
  ~InteractionEpoch();

  InteractionEpoch(const InteractionEpoch&) = delete;
  InteractionEpoch& operator=(const InteractionEpoch&) = delete;

  uint64_t id() const { return id_; }
  uint64_t current_tick() const { return tick_; }

  // Fan the pointer out to the primary + every derived session.
  void UpdatePointer(POINT pointer_screen);

  // Queue a fracture (called by the primary session during detection). Snapshot
  // is immutable; suppressed if the surface already has a pending fracture or if
  // we're currently applying. NOT applied here — applied at the tick boundary.
  void RequestFracture(SurfaceShell* member,
                       const morphic::ExtractionIntent& intent);

  // PHASE 8D — queue a dock (called by the primary session during dock detection).
  // Immutable snapshot; suppressed if the same source+target pair is already
  // pending or if we're currently applying. NOT applied here — applied at the tick
  // boundary, after fractures, with a re-validation that both are still detached
  // and still in range.
  void RequestDock(SurfaceShell* source, SurfaceShell* target,
                   const morphic::DockingIntent& intent);

  // End: snap all sessions to target, project final, commit the ONE transaction
  // once, resolve every extracted surface's tether → Detached, publish events.
  void End();
  // Cancel: discard UNAPPLIED pending fractures; already-applied derived sessions
  // keep their projected geometry (their topology mutation already became truth);
  // commit the one transaction.
  void Cancel(const char* reason);

  void OnDpiChangedLeader(const RECT& new_bounds);

  // Forwarded to the primary session (router query contract).
  InteractionSession* primary() const { return primary_.get(); }
  SurfaceShell* leader() const;
  InteractionMode mode() const;
  bool subscribed_to_clock() const { return clock_token_ != 0; }
  RECT target_bounds() const;
  std::optional<RECT> presented_bounds() const;
  InteractionSession::Velocity current_velocity() const;
  const InteractionSession::Metrics& last_metrics() const { return last_metrics_; }
  size_t derived_count() const { return derived_.size(); }

 private:
  struct PendingFracture {
    SurfaceShell* member;
    morphic::ExtractionIntent intent_snapshot;  // immutable copy
  };
  struct PendingDock {
    SurfaceShell* source;
    SurfaceShell* target;
    morphic::DockingIntent intent_snapshot;  // immutable copy
  };

  // The single clock tick: drive primary, then each derived (by construction
  // order = primary precedence), then apply pending fractures, then docks, then
  // reap empties. Topology mutation ONLY happens here, at the boundary.
  void OnTick(double dt_ms);
  void ApplyPendingFractures();
  void ApplyPendingDocks();
  void ReapEmptySessions();
  void Commit(bool snap_to_target, const char* reason);  // shared End/Cancel body

  SurfaceModel* model_;
  FrameClock* clock_;
  EventBus* bus_;
  SurfaceGraph* graph_;
  PresentationCoordinator* coordinator_;  // PHASE 8E — passed to sessions (settle)
  morphic::ExtractionThresholds thresholds_;
  morphic::DockingThresholds dock_thresholds_;
  FractureObserver on_fracture_;
  DockObserver on_dock_;

  uint64_t id_;
  uint64_t tick_ = 0;
  bool applying_ = false;   // true while ApplyPendingFractures runs (reject enqueue)
  bool committed_ = false;  // guards double End/Cancel

  std::unique_ptr<InteractionSession> primary_;
  std::vector<std::unique_ptr<InteractionSession>> derived_;
  std::vector<PendingFracture> pending_;
  std::vector<PendingDock> pending_docks_;

  FrameClock::Token clock_token_ = 0;
  InteractionSession::Metrics last_metrics_{};

  static uint64_t next_id_;
};

#endif  // RUNNER_MULTISURFACE_INTERACTION_EPOCH_H_
