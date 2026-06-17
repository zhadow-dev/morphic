#ifndef RUNNER_MULTISURFACE_INTERACTION_SESSION_H_
#define RUNNER_MULTISURFACE_INTERACTION_SESSION_H_

#include <windows.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

#include <functional>

#include "frame_clock.h"
#include "multisurface/docking_model.h"     // DockingState / Intent / Thresholds
#include "multisurface/extraction_model.h"  // ExtractionState / Intent / Thresholds
#include "runtime_events.h"  // EventBus::Token

class EventBus;
class SurfaceModel;
class SurfaceShell;
class SurfaceGraph;
class PresentationCoordinator;

// PHASE 8B — InteractionMode
//
// Re-declared here (used to live only in interaction_router.h). The session is
// now the authoritative type that carries the interaction mode; the router
// just exposes a forwarder.
enum class InteractionMode {
  None,
  Dragging,
  Resizing,
};

// PHASE 8B — InteractionSession
//
// First-class runtime object for interaction orchestration. The session is
// the SOLE owner during an active interaction of:
//   - participants (leader + 0..N additional grouped members)
//   - per-member baseline (RECT snapshotted at session start; IMMUTABLE for
//     the session's lifetime)
//   - one shared SurfaceModel transaction
//   - one shared FrameClock subscription
//   - geometry projection across all members
//   - lifecycle events (InteractionBegan/Updated/Ended, SurfaceMoved/Resized)
//
// Determinism rule: every member's new bounds = member.origin_bounds +
// leader_delta. There is NO state where member B's position depends on member
// A's projected position. This is the difference between "deterministic
// baselines" and the rounding-accumulator drift that kills naive group-move
// implementations.
//
// 1-member sessions are the common case (ungrouped drag/resize). They share
// the same code path as N-member sessions — the collapse is exact.
//
// N-member sessions exist for group MOVE (Dragging mode only). Resize stays
// 1-member: group resize semantics (anchor selection, aspect preservation,
// member proportionality) is a design problem deferred to 8C+. The router
// enforces this by creating a single-member session whenever
// mode = Resizing, regardless of group membership.
class InteractionSession {
 public:
  struct MemberBaseline {
    SurfaceShell* surface;
    RECT origin_bounds;
    std::optional<RECT> last_projected;  // for per-member coalesce-drop
    // PHASE 8C — per-member extraction state (session-local PHASE storage, not
    // long-term topology truth; may migrate to the relationship layer later).
    morphic::ExtractionState extraction = morphic::ExtractionState::None;
    int sustained_divergence_ticks = 0;  // ticks at/over extract distance
    int sustained_release_ticks = 0;     // ticks under release distance (decay)
    bool fracture_queued = false;        // frozen: stop rescoring once queued
  };

  struct Velocity {
    double x_px_per_s = 0.0;
    double y_px_per_s = 0.0;
    double magnitude_px_per_s = 0.0;
  };

  struct Metrics {
    int pointer_events = 0;        // UpdatePointer calls
    int ticks = 0;                 // OnFrameTick calls
    int projections = 0;           // SetBounds calls actually issued (summed across members)
    int dropped_projections = 0;   // per-member coalesce-drops
    int reentrant_drops = 0;       // delta in SurfaceModel::reentrant_drops()
    int samples_collected = 0;
    int member_count = 0;          // size of participant set
    double duration_ms = 0.0;
    double avg_tick_interval_ms = 0.0;
    double peak_tick_interval_ms = 0.0;
    double peak_velocity_px_per_s = 0.0;
    double final_velocity_px_per_s = 0.0;
    double final_lag_px = 0.0;
    int extractions = 0;                 // PHASE 8C — members this session fractured off
    double peak_extraction_score = 0.0;  // PHASE 8C
    int docks = 0;                       // PHASE 8D — docks applied this interaction
    double peak_docking_score = 0.0;     // PHASE 8D
  };

  // Direct-manipulation contract: presented = target every tick (see 7E rev3).
  // The smoothing infrastructure stays as a knob for future programmatic
  // motion subsystems but is NEVER active for pointer-driven interactions.
  static constexpr double kPresentationTauMs = 24.0;
  static constexpr double kSnapThresholdPx = 1.0;
  static constexpr bool kPresentationSmoothingEnabled = false;
  static constexpr size_t kSampleCapacity = 16;

  // PHASE 8C — the session no longer owns the transaction or the clock
  // subscription (the InteractionEpoch does — one per interaction, shared across
  // primary + derived). The session publishes InteractionBegan and snapshots
  // baselines. `leader` is the surface the user clicked; `members` contains the
  // leader AND every other group member (for an ungrouped surface, {leader}).
  //
  // `is_derived` marks an EXTRACTED session spun off mid-fracture: it is always
  // 1-member, never self-extracts (detection disabled), and records its
  // `parent_session` for lineage. `on_request_fracture` is invoked by a PRIMARY
  // session when a non-leader member crosses the extraction threshold — it QUEUES
  // a fracture on the epoch (member is NOT erased here).
  using FractureRequest =
      std::function<void(SurfaceShell* member, const morphic::ExtractionIntent&)>;
  // PHASE 8D — a PRIMARY single-member detached drag requests a dock when it
  // converges (proximity + overlap, sustained) onto a detached target. QUEUES on
  // the epoch (applied at the tick boundary); the surfaces are NOT grouped here.
  using DockRequest =
      std::function<void(SurfaceShell* source, SurfaceShell* target,
                         const morphic::DockingIntent&)>;

  InteractionSession(SurfaceModel* model, EventBus* bus, SurfaceGraph* graph,
                     PresentationCoordinator* coordinator, InteractionMode mode,
                     SurfaceShell* leader, std::vector<SurfaceShell*> members,
                     POINT pointer_origin, int resize_edge, bool is_derived,
                     InteractionSession* parent_session,
                     const morphic::ExtractionThresholds& thresholds,
                     const morphic::DockingThresholds& dock_thresholds,
                     FractureRequest on_request_fracture,
                     DockRequest on_request_dock);
  ~InteractionSession();

  InteractionSession(const InteractionSession&) = delete;
  InteractionSession& operator=(const InteractionSession&) = delete;

  void UpdatePointer(POINT pointer_screen);

  // PHASE 8C — driven by the epoch's single clock tick (was private + clock-
  // subscribed). One call per epoch tick.
  void OnFrameTick(double dt_ms);

  // PHASE 8C — remove `member` from this session's participant set (called by the
  // epoch at the tick boundary when applying a fracture). Returns the member's
  // current bounds so the epoch can baseline the derived session. No-op +
  // returns origin if not a member.
  RECT RemoveMember(SurfaceShell* member);

  // PHASE 8C — End no longer commits the transaction (the epoch does, once).
  // Snaps presentation to target, projects final geometry, finalizes metrics,
  // publishes Ended + SurfaceMoved|SurfaceResized for this session's members.
  void End();

  // PHASE 8C — Cancel no longer commits (epoch does). Finalizes at last projected.
  void Cancel(const char* reason);

  // DPI changed mid-session for the leader — recapture origin so subsequent
  // deltas start from zero against the new scale.
  void OnDpiChangedLeader(const RECT& new_bounds);

  InteractionMode mode() const { return mode_; }
  SurfaceShell* leader() const { return leader_; }
  const std::vector<MemberBaseline>& members() const { return members_; }
  size_t member_count() const { return members_.size(); }
  bool is_derived() const { return is_derived_; }
  InteractionSession* parent_session() const { return parent_session_; }
  bool active() const { return active_; }
  RECT target_bounds() const { return target_bounds_; }
  std::optional<RECT> presented_bounds() const { return presented_bounds_; }
  Velocity current_velocity() const;
  const Metrics& metrics() const { return metrics_; }

 private:
  struct InputSample {
    RECT bounds{};
    LARGE_INTEGER timestamp{};
  };

  // PHASE 8C — extraction detection (primary, non-derived sessions only).
  void DetectExtraction();
  // PHASE 8D — dock detection (primary, single-member, DETACHED drag only). Scans
  // model_->z_order() for the best detached target the dragged leader is
  // converging onto; queues a dock on the epoch when intent is sustained.
  void DetectDocking();
  RECT ComputeLeaderTarget(POINT pointer_screen) const;
  // Returns the projected bounds for `m` given the leader's current bounds.
  // For Dragging: m.origin_bounds + (leader - leader.origin_bounds).
  // For Resizing: leader only (m is always leader in resize sessions).
  RECT MemberBoundsFor(const MemberBaseline& m,
                        const RECT& leader_bounds) const;
  bool AdvanceAndProject(double dt_ms);
  void PushSample(const RECT& bounds);
  const InputSample& Sample(size_t i) const;
  void FinalizeMetrics();
  void TearDown();
  // Group centroid (mean of member centers) + group velocity (mean of member
  // velocities) — a heuristic extraction aid, NOT topology authority.
  void GroupKinematics(double* cx, double* cy, double* vx, double* vy) const;

  SurfaceModel* model_;
  EventBus* bus_;
  SurfaceGraph* graph_;  // PHASE 8D — read-only (detached check for dock detection)
  PresentationCoordinator* coordinator_;  // PHASE 8E — settle projection (null = hard snap)

  InteractionMode mode_;
  bool is_derived_;
  InteractionSession* parent_session_;  // null for primary
  morphic::ExtractionThresholds thresholds_;
  morphic::DockingThresholds dock_thresholds_;  // PHASE 8D
  FractureRequest on_request_fracture_;  // null on derived sessions
  DockRequest on_request_dock_;          // PHASE 8D — null on derived sessions

  // PHASE 8D — dock candidate tracking (single-member sessions). We track the
  // current best target + sustained tick counter with hysteresis decay so a
  // fly-by doesn't dock and boundary jitter doesn't flicker. `dock_queued_`
  // freezes once a dock is requested (one dock per drag).
  SurfaceShell* dock_target_ = nullptr;
  int dock_sustained_ticks_ = 0;
  int dock_release_ticks_ = 0;
  morphic::DockingState dock_state_ = morphic::DockingState::None;
  bool dock_queued_ = false;
  SurfaceShell* leader_;
  std::vector<MemberBaseline> members_;
  POINT pointer_origin_;
  RECT origin_bounds_leader_;
  int resize_edge_;

  RECT target_bounds_{};
  std::optional<RECT> presented_bounds_;

  std::array<InputSample, kSampleCapacity> samples_{};
  size_t sample_head_ = 0;
  size_t sample_count_ = 0;

  EventBus::Token bus_token_ = 0;  // PHASE 8B.5 — for SurfaceDestroyed prune
  Metrics metrics_{};
  LARGE_INTEGER start_qpc_{};
  LARGE_INTEGER last_tick_qpc_{};
  int reentrant_baseline_ = 0;
  bool active_ = true;  // becomes false in End/Cancel — guards re-entry
};

#endif  // RUNNER_MULTISURFACE_INTERACTION_SESSION_H_
