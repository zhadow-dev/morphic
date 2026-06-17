#include "multisurface/interaction_session.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>

#include "forensic_trace.h"
#include "runtime_events.h"
#include "multisurface/surface_graph.h"
#include "presentation/presentation_coordinator.h"
#include "surface_model.h"
#include "surface_shell.h"

namespace {

constexpr int kMinWidth = 200;
constexpr int kMinHeight = 150;

const char* ModeName(InteractionMode mode) {
  switch (mode) {
    case InteractionMode::None:     return "none";
    case InteractionMode::Dragging: return "drag";
    case InteractionMode::Resizing: return "resize";
  }
  return "?";
}

std::string IdOf(SurfaceShell* s) { return s ? s->id() : std::string("<none>"); }

bool RectEq(const RECT& a, const RECT& b) {
  return a.left == b.left && a.top == b.top && a.right == b.right &&
         a.bottom == b.bottom;
}

double ElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) {
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  if (freq.QuadPart == 0) return 0.0;
  return (end.QuadPart - start.QuadPart) * 1000.0 /
         static_cast<double>(freq.QuadPart);
}

POINT Center(const RECT& r) {
  return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
}

}  // namespace

InteractionSession::InteractionSession(
    SurfaceModel* model, EventBus* bus, SurfaceGraph* graph,
    PresentationCoordinator* coordinator, InteractionMode mode,
    SurfaceShell* leader, std::vector<SurfaceShell*> members,
    POINT pointer_origin, int resize_edge, bool is_derived,
    InteractionSession* parent_session,
    const morphic::ExtractionThresholds& thresholds,
    const morphic::DockingThresholds& dock_thresholds,
    FractureRequest on_request_fracture, DockRequest on_request_dock)
    : model_(model), bus_(bus), graph_(graph), coordinator_(coordinator),
      mode_(mode), is_derived_(is_derived), parent_session_(parent_session),
      thresholds_(thresholds), dock_thresholds_(dock_thresholds),
      on_request_fracture_(std::move(on_request_fracture)),
      on_request_dock_(std::move(on_request_dock)), leader_(leader),
      pointer_origin_(pointer_origin), resize_edge_(resize_edge) {
  // Snapshot baselines. Each member gets its OWN origin_bounds at session
  // start; deltas are computed against this immutable snapshot every tick.
  // Leader is always members[0] by convention so the auditor can correlate.
  members_.reserve(members.size());
  for (SurfaceShell* s : members) {
    MemberBaseline b{};
    b.surface = s;
    b.origin_bounds = model_ ? model_->bounds(s) : RECT{};
    members_.push_back(b);
    if (s == leader_) origin_bounds_leader_ = b.origin_bounds;
  }
  // Defensive: if leader wasn't in members (shouldn't happen), capture its
  // bounds anyway.
  if (origin_bounds_leader_.right == 0 && origin_bounds_leader_.bottom == 0 &&
      leader_ && model_) {
    origin_bounds_leader_ = model_->bounds(leader_);
  }

  target_bounds_ = origin_bounds_leader_;
  presented_bounds_ = origin_bounds_leader_;

  // PHASE 8E (I-E2) — direct manipulation SUPERSEDES any in-flight settle. If a
  // member is mid-settle from a previous interaction, drop it now so the new drag
  // is strictly 1:1 with no presentation fighting the pointer.
  if (coordinator_) {
    for (auto& m : members_) {
      if (m.surface) coordinator_->CancelSettle(m.surface);
    }
  }

  metrics_.member_count = static_cast<int>(members_.size());
  reentrant_baseline_ = model_ ? model_->reentrant_drops() : 0;
  QueryPerformanceCounter(&start_qpc_);
  last_tick_qpc_.QuadPart = 0;

  PushSample(origin_bounds_leader_);

  forensic::Log("SESSION",
                "Begin " + std::string(ModeName(mode_)) +
                    (is_derived_ ? " (derived)" : "") +
                    " leader=" + IdOf(leader_) +
                    " members=" + std::to_string(members_.size()) +
                    " origin=(" + std::to_string(pointer_origin_.x) + "," +
                    std::to_string(pointer_origin_.y) + ") bounds=(" +
                    std::to_string(origin_bounds_leader_.left) + "," +
                    std::to_string(origin_bounds_leader_.top) + " " +
                    std::to_string(origin_bounds_leader_.right -
                                   origin_bounds_leader_.left) +
                    "x" +
                    std::to_string(origin_bounds_leader_.bottom -
                                   origin_bounds_leader_.top) +
                    ")");

  // PHASE 8C — the InteractionEpoch owns the ONE transaction + ONE clock
  // subscription for the whole interaction (shared across primary + derived).
  // The session no longer opens its own — it is ticked by the epoch and projects
  // into the epoch's transaction.
  if (bus_) {
    // PHASE 8B.5 — listen for SurfaceDestroyed so a follower being destroyed
    // mid-session can be pruned from members_ before the next tick tries to
    // SetBounds on a dying pointer. Leader destruction is handled separately
    // by the router (WM_DESTROY → CancelInteraction).
    bus_token_ = bus_->Subscribe(
        [this](RuntimeEvent e, SurfaceShell* s) {
          if (e == RuntimeEvent::SurfaceDestroyed && active_ && s != nullptr &&
              s != leader_) {
            const size_t before = members_.size();
            members_.erase(std::remove_if(members_.begin(), members_.end(),
                                          [s](const MemberBaseline& m) {
                                            return m.surface == s;
                                          }),
                           members_.end());
            if (members_.size() != before) {
              forensic::Log("SESSION",
                            "follower destroyed mid-session — pruned id=" +
                                IdOf(s) +
                                " remaining=" +
                                std::to_string(members_.size()));
            }
          }
        });
    bus_->Publish(RuntimeEvent::InteractionBegan, leader_);
  }
}

InteractionSession::~InteractionSession() {
  // PHASE 8C — defensive teardown if neither End nor Cancel ran. The session no
  // longer owns the transaction (the epoch commits it once), so here we only
  // unsubscribe our bus token. The epoch's destructor handles the transaction.
  if (active_) {
    forensic::Log("SESSION",
                  "destructor without End/Cancel — emergency teardown");
    TearDown();
  }
}

void InteractionSession::UpdatePointer(POINT pointer_screen) {
  if (!active_) return;
  target_bounds_ = ComputeLeaderTarget(pointer_screen);
  ++metrics_.pointer_events;
  PushSample(target_bounds_);
}

void InteractionSession::OnFrameTick(double dt_ms) {
  if (!active_) return;
  ++metrics_.ticks;

  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if (last_tick_qpc_.QuadPart != 0) {
    const double gap = ElapsedMs(last_tick_qpc_, now);
    metrics_.avg_tick_interval_ms += gap;
    if (gap > metrics_.peak_tick_interval_ms) {
      metrics_.peak_tick_interval_ms = gap;
    }
  }
  last_tick_qpc_ = now;

  const Velocity v = current_velocity();
  if (v.magnitude_px_per_s > metrics_.peak_velocity_px_per_s) {
    metrics_.peak_velocity_px_per_s = v.magnitude_px_per_s;
  }

  const bool any_projected = AdvanceAndProject(dt_ms);
  if (any_projected && bus_) {
    bus_->Publish(RuntimeEvent::InteractionUpdated, leader_);
  }

  // PHASE 8C — extraction detection. PRIMARY sessions only: a derived session
  // never self-extracts (single fracture depth — detection skipped entirely so
  // there is no wasted scoring / hidden state / recursion hazard).
  if (!is_derived_ && on_request_fracture_) {
    DetectExtraction();
  }

  // PHASE 8D — dock detection. PRIMARY, single-member, DETACHED drag only (a
  // derived session never docks — single depth). Scoped exactly like extraction:
  // skipped entirely when derived / multi-member / grouped.
  if (!is_derived_ && on_request_dock_) {
    DetectDocking();
  }
}

bool InteractionSession::AdvanceAndProject(double dt_ms) {
  if (!presented_bounds_ || !model_) return false;

  // Direct manipulation = 1:1. Smoothing path retained for future programmatic
  // motion subsystems (see 7E rev3 architectural note).
  double alpha;
  if constexpr (kPresentationSmoothingEnabled) {
    alpha = (dt_ms > 0.0) ? (1.0 - std::exp(-dt_ms / kPresentationTauMs)) : 0.0;
  } else {
    alpha = 1.0;
  }

  auto blend = [alpha](LONG p, LONG t) -> LONG {
    return static_cast<LONG>(std::lround(
        static_cast<double>(p) +
        (static_cast<double>(t) - static_cast<double>(p)) * alpha));
  };
  RECT advanced = {
      blend(presented_bounds_->left,   target_bounds_.left),
      blend(presented_bounds_->top,    target_bounds_.top),
      blend(presented_bounds_->right,  target_bounds_.right),
      blend(presented_bounds_->bottom, target_bounds_.bottom),
  };
  auto snap = [](LONG p, LONG t) -> LONG {
    return (std::abs(static_cast<long>(p - t)) <=
            static_cast<long>(kSnapThresholdPx))
               ? t
               : p;
  };
  advanced.left   = snap(advanced.left,   target_bounds_.left);
  advanced.top    = snap(advanced.top,    target_bounds_.top);
  advanced.right  = snap(advanced.right,  target_bounds_.right);
  advanced.bottom = snap(advanced.bottom, target_bounds_.bottom);

  presented_bounds_ = advanced;

  // Project EVERY member. Each member's bounds derive from its own baseline
  // + the leader's delta — no chained deltas. Per-member coalesce-drop so a
  // member whose bounds haven't perceptibly changed skips its SetBounds.
  bool any = false;
  for (auto& m : members_) {
    // PHASE 8B.5 — defensive: skip dead members. The bus-driven prune above
    // catches normal SurfaceDestroyed but a member's HWND can go null in the
    // brief window between WM_NCDESTROY and our subscriber firing.
    if (m.surface == nullptr || m.surface->GetHandle() == nullptr) continue;
    const RECT m_bounds = MemberBoundsFor(m, advanced);
    if (m.last_projected && RectEq(*m.last_projected, m_bounds)) {
      ++metrics_.dropped_projections;
      continue;
    }
    model_->SetBounds(m.surface, m_bounds);
    m.last_projected = m_bounds;
    ++metrics_.projections;
    any = true;
  }
  return any;
}

void InteractionSession::End() {
  if (!active_) return;

  // Snap-to-target for final geometry (presented may lag if smoothing is
  // ever re-enabled). With direct manipulation 1:1 this is a no-op for diff,
  // but final_lag_px is still recorded so the metric remains meaningful in
  // any future hybrid path.
  if (presented_bounds_) {
    const double dx = static_cast<double>(target_bounds_.left -
                                          presented_bounds_->left);
    const double dy = static_cast<double>(target_bounds_.top -
                                          presented_bounds_->top);
    metrics_.final_lag_px = std::sqrt(dx * dx + dy * dy);
  }
  presented_bounds_ = target_bounds_;

  // Force one final projection per member at target so native = cursor-at-
  // release for every participant. No-op when member's last_projected already
  // matches.
  //
  // PHASE 8E — SETTLE SEAM. If the final semantic position differs from where the
  // window VISIBLY is (m.last_projected — e.g. a reachability-clamp correction at
  // release, or coalesce-drop residue), hand the residual to the
  // PresentationCoordinator to EASE native → semantic over a few ticks instead of
  // a hard jump. Direct manipulation during the drag was already 1:1; this only
  // smooths the END discontinuity. When there's no residual (the common case),
  // hard-project exactly as before. Resize never settles (edges must land exact).
  for (auto& m : members_) {
    if (m.surface == nullptr || m.surface->GetHandle() == nullptr) continue;
    const RECT m_final = MemberBoundsFor(m, target_bounds_);
    if (m.last_projected && RectEq(*m.last_projected, m_final)) continue;

    const bool can_settle =
        coordinator_ != nullptr && mode_ == InteractionMode::Dragging &&
        m.last_projected.has_value();
    if (can_settle && model_) {
      // Set semantic truth WITHOUT projecting; the coordinator eases native from
      // last_projected toward the (clamped) semantic final.
      model_->SetSemanticBounds(m.surface, m_final);
      coordinator_->RequestSettle(m.surface, *m.last_projected);
      ++metrics_.projections;
    } else if (model_) {
      model_->SetBounds(m.surface, m_final);  // hard project (no coordinator/residual baseline)
      ++metrics_.projections;
    }
    m.last_projected = m_final;
  }

  const Velocity v = current_velocity();
  metrics_.final_velocity_px_per_s = v.magnitude_px_per_s;
  FinalizeMetrics();

  // PHASE 8C — the epoch commits the ONE shared transaction once, after every
  // session has projected its final geometry. The session only tears down its own
  // bus subscription here.
  TearDown();

  if (bus_) {
    bus_->Publish(RuntimeEvent::InteractionEnded, leader_);
    bus_->Publish(mode_ == InteractionMode::Resizing
                      ? RuntimeEvent::SurfaceResized
                      : RuntimeEvent::SurfaceMoved,
                  leader_);
    // For multi-member sessions, publish SurfaceMoved for each non-leader
    // member too — observers that track per-surface moves shouldn't miss
    // group followers. PHASE 8B.5: skip pruned/dead members.
    for (auto& m : members_) {
      if (m.surface != leader_ && m.surface != nullptr &&
          m.surface->GetHandle() != nullptr) {
        bus_->Publish(RuntimeEvent::SurfaceMoved, m.surface);
      }
    }
  }
}

void InteractionSession::Cancel(const char* reason) {
  if (!active_) return;
  forensic::Log("SESSION", "Cancel leader=" + IdOf(leader_) +
                              " reason=" + std::string(reason ? reason : "?") +
                              " mode=" + ModeName(mode_));

  const Velocity v = current_velocity();
  metrics_.final_velocity_px_per_s = v.magnitude_px_per_s;
  FinalizeMetrics();

  // PHASE 8C — epoch commits the shared transaction; session only unsubscribes.
  TearDown();

  if (bus_) {
    bus_->Publish(RuntimeEvent::InteractionEnded, leader_);
  }
}

void InteractionSession::OnDpiChangedLeader(const RECT& new_bounds) {
  if (!active_) return;
  POINT cursor{};
  if (!GetCursorPos(&cursor)) {
    forensic::Log("SESSION",
                  "OnDpiChangedLeader: GetCursorPos failed; origin NOT "
                  "recaptured (lag will accumulate)");
    return;
  }
  forensic::Log("SESSION",
                "OnDpiChangedLeader id=" + IdOf(leader_) + " — recapture");
  pointer_origin_ = cursor;
  origin_bounds_leader_ = new_bounds;
  target_bounds_ = new_bounds;
  presented_bounds_ = new_bounds;
  // Recapture every member's baseline too — they all need their own new
  // baseline so subsequent deltas don't apply cross-DPI offsets.
  for (auto& m : members_) {
    if (m.surface == leader_) {
      m.origin_bounds = new_bounds;
    } else if (model_) {
      m.origin_bounds = model_->bounds(m.surface);
    }
    m.last_projected.reset();  // force one fresh projection on next tick
  }
  sample_head_ = 0;
  sample_count_ = 0;
  PushSample(new_bounds);
}

void InteractionSession::TearDown() {
  active_ = false;
  // PHASE 8C — the clock subscription belongs to the epoch now; the session only
  // owns its bus token (the 8B.5 destroyed-member prune subscription).
  if (bus_ && bus_token_ != 0) {
    bus_->Unsubscribe(bus_token_);
    bus_token_ = 0;
  }
}

RECT InteractionSession::ComputeLeaderTarget(POINT pointer_screen) const {
  const LONG dx = pointer_screen.x - pointer_origin_.x;
  const LONG dy = pointer_screen.y - pointer_origin_.y;
  RECT r = origin_bounds_leader_;

  if (mode_ == InteractionMode::Dragging) {
    const LONG w = origin_bounds_leader_.right - origin_bounds_leader_.left;
    const LONG h = origin_bounds_leader_.bottom - origin_bounds_leader_.top;
    r.left = origin_bounds_leader_.left + dx;
    r.top = origin_bounds_leader_.top + dy;
    r.right = r.left + w;
    r.bottom = r.top + h;
    return r;
  }

  // Resizing — single member by construction. Same per-edge math as 7A.
  const bool left   = (resize_edge_ == HTLEFT  || resize_edge_ == HTTOPLEFT    || resize_edge_ == HTBOTTOMLEFT);
  const bool right  = (resize_edge_ == HTRIGHT || resize_edge_ == HTTOPRIGHT   || resize_edge_ == HTBOTTOMRIGHT);
  const bool top    = (resize_edge_ == HTTOP   || resize_edge_ == HTTOPLEFT    || resize_edge_ == HTTOPRIGHT);
  const bool bottom = (resize_edge_ == HTBOTTOM|| resize_edge_ == HTBOTTOMLEFT || resize_edge_ == HTBOTTOMRIGHT);

  if (left)   r.left   = std::min<LONG>(origin_bounds_leader_.left + dx, origin_bounds_leader_.right - kMinWidth);
  if (right)  r.right  = std::max<LONG>(origin_bounds_leader_.right + dx, origin_bounds_leader_.left + kMinWidth);
  if (top)    r.top    = std::min<LONG>(origin_bounds_leader_.top + dy, origin_bounds_leader_.bottom - kMinHeight);
  if (bottom) r.bottom = std::max<LONG>(origin_bounds_leader_.bottom + dy, origin_bounds_leader_.top + kMinHeight);
  return r;
}

RECT InteractionSession::MemberBoundsFor(const MemberBaseline& m,
                                          const RECT& leader_bounds) const {
  if (mode_ == InteractionMode::Resizing) {
    // 8B constraint: resize sessions are always 1-member (the leader).
    // For safety/robustness, non-leader members in a resize session don't
    // move — they keep their baseline.
    return m.surface == leader_ ? leader_bounds : m.origin_bounds;
  }
  // Dragging: derive m's bounds from m's OWN baseline + leader's delta.
  // This is the determinism rule — m never depends on any other member's
  // projected state, only on leader_bounds (which itself derives from
  // pointer delta + leader.origin_bounds).
  const LONG dx = leader_bounds.left - origin_bounds_leader_.left;
  const LONG dy = leader_bounds.top - origin_bounds_leader_.top;
  RECT r = m.origin_bounds;
  const LONG w = r.right - r.left;
  const LONG h = r.bottom - r.top;
  r.left += dx;
  r.top += dy;
  r.right = r.left + w;
  r.bottom = r.top + h;
  return r;
}

void InteractionSession::PushSample(const RECT& bounds) {
  InputSample s{};
  s.bounds = bounds;
  QueryPerformanceCounter(&s.timestamp);
  samples_[sample_head_] = s;
  sample_head_ = (sample_head_ + 1) % kSampleCapacity;
  if (sample_count_ < kSampleCapacity) ++sample_count_;
  ++metrics_.samples_collected;
}

const InteractionSession::InputSample& InteractionSession::Sample(
    size_t i) const {
  const size_t start = (sample_count_ < kSampleCapacity) ? 0 : sample_head_;
  return samples_[(start + i) % kSampleCapacity];
}

InteractionSession::Velocity InteractionSession::current_velocity() const {
  Velocity v{};
  if (sample_count_ < 2) return v;
  const InputSample& oldest = Sample(0);
  const InputSample& newest = Sample(sample_count_ - 1);
  const double dt_s = ElapsedMs(oldest.timestamp, newest.timestamp) / 1000.0;
  if (dt_s <= 0.0) return v;
  const POINT a = Center(oldest.bounds);
  const POINT b = Center(newest.bounds);
  v.x_px_per_s = (b.x - a.x) / dt_s;
  v.y_px_per_s = (b.y - a.y) / dt_s;
  v.magnitude_px_per_s =
      std::sqrt(v.x_px_per_s * v.x_px_per_s + v.y_px_per_s * v.y_px_per_s);
  return v;
}

void InteractionSession::FinalizeMetrics() {
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  if (start_qpc_.QuadPart != 0) {
    metrics_.duration_ms = ElapsedMs(start_qpc_, now);
  }
  if (metrics_.ticks > 1) {
    metrics_.avg_tick_interval_ms /= (metrics_.ticks - 1);
  } else {
    metrics_.avg_tick_interval_ms = 0.0;
  }
  metrics_.reentrant_drops =
      model_ ? (model_->reentrant_drops() - reentrant_baseline_) : 0;

  char buf[416];
  _snprintf_s(buf, sizeof(buf), _TRUNCATE,
              "leader=%s members=%d duration=%.1fms ptr_events=%d ticks=%d "
              "projections=%d dropped=%d reentrant=%d samples=%d "
              "avg_tick=%.2fms peak_tick=%.2fms peak_v=%.0fpx/s "
              "final_v=%.0fpx/s final_lag=%.1fpx",
              leader_ ? leader_->id().c_str() : "?", metrics_.member_count,
              metrics_.duration_ms, metrics_.pointer_events, metrics_.ticks,
              metrics_.projections, metrics_.dropped_projections,
              metrics_.reentrant_drops, metrics_.samples_collected,
              metrics_.avg_tick_interval_ms, metrics_.peak_tick_interval_ms,
              metrics_.peak_velocity_px_per_s,
              metrics_.final_velocity_px_per_s, metrics_.final_lag_px);
  forensic::Log("METRICS", buf);
}

// ===========================================================================
// PHASE 8C — extraction detection
// ===========================================================================

void InteractionSession::GroupKinematics(double* cx, double* cy, double* vx,
                                          double* vy) const {
  // Heuristic aid, NOT topology authority: mean of member CENTERS (current
  // target geometry) and a coarse group velocity proxy (the leader's velocity,
  // since all members share the leader's delta during a group drag). This keeps
  // the per-member relative-velocity meaningful (a member at rest relative to
  // the group scores ~0 velocity).
  double sx = 0.0, sy = 0.0;
  int n = 0;
  for (const auto& m : members_) {
    if (m.surface == nullptr || m.surface->GetHandle() == nullptr) continue;
    const RECT b = MemberBoundsFor(m, target_bounds_);
    sx += (b.left + b.right) * 0.5;
    sy += (b.top + b.bottom) * 0.5;
    ++n;
  }
  *cx = n > 0 ? sx / n : 0.0;
  *cy = n > 0 ? sy / n : 0.0;
  const Velocity gv = current_velocity();  // leader-center velocity == group drift
  *vx = gv.x_px_per_s;
  *vy = gv.y_px_per_s;
}

void InteractionSession::DetectExtraction() {
  // ===========================================================================
  // DORMANT BY DESIGN (8C). This detector does NOT fire under the current
  // interaction model, and that is intentional — not a bug.
  //
  // WHY: grouped drag moves every member RIGIDLY by the leader's shared delta
  // (the determinism rule). So a non-leader member's distance from the group's
  // LIVE centroid is INVARIANT during a group drag — `dist_px` stays ~0 and never
  // crosses extract_dist_px. Extraction (pull ONE surface out of a group) is a
  // DISTINCT gesture from grouped translation (move the group together); the two
  // are different user intentions. We deliberately do NOT distort rigid-group
  // semantics to force this detector to trigger.
  //
  // WHAT REMAINS LIVE: the scoring model (extraction_model.h, unit-validated), the
  // epoch fracture machinery (RequestFracture → tick-boundary apply → derived
  // session), and the TopologyMutated/GroupFractured events are all in place and
  // reachable. They are waiting for a real extraction GESTURE, which is deferred
  // to 8D/8E (alongside docking/regroup semantics). This function stays wired
  // (it computes ~0 and never queues) so the fracture path stays compiled and
  // available for manual testing once a gesture exists.
  // ===========================================================================

  // PRIMARY, dragging sessions only. Resize sessions are 1-member and never
  // group-extract; a 1-member drag has no non-leader members to extract.
  if (mode_ != InteractionMode::Dragging || members_.size() < 2) return;

  double gcx, gcy, gvx, gvy;
  GroupKinematics(&gcx, &gcy, &gvx, &gvy);

  // Per-member relative velocity ≈ member velocity − group velocity. During a
  // rigid group drag every member moves with the leader, so relative velocity is
  // ~0 until the user pulls one away faster than the group (the escape impulse).
  const Velocity leader_v = current_velocity();

  for (auto& m : members_) {
    if (m.surface == leader_) continue;             // leader never self-extracts
    if (m.fracture_queued) continue;                // frozen once queued
    if (m.surface == nullptr || m.surface->GetHandle() == nullptr) continue;

    const RECT b = MemberBoundsFor(m, target_bounds_);
    const double mcx = (b.left + b.right) * 0.5;
    const double mcy = (b.top + b.bottom) * 0.5;

    // Member velocity: in a rigid group it equals the leader's. The escape
    // signal comes from DISTANCE growth as the user drags the pointer; the
    // velocity term captures fast pulls. We approximate member velocity as the
    // leader velocity (shared delta) — relative velocity is then the leader's
    // motion away from the centroid for THIS member's escape direction.
    const morphic::ExtractionIntent intent = morphic::EvaluateExtraction(
        mcx, mcy, leader_v.x_px_per_s, leader_v.y_px_per_s, gcx, gcy, gvx, gvy,
        m.sustained_divergence_ticks, thresholds_);

    if (intent.attribution.score > metrics_.peak_extraction_score) {
      metrics_.peak_extraction_score = intent.attribution.score;
    }

    // Sustained / decay counters (hysteresis, anti-flicker).
    if (intent.attribution.dist_px >= thresholds_.extract_dist_px) {
      ++m.sustained_divergence_ticks;
      m.sustained_release_ticks = 0;
      if (m.extraction == morphic::ExtractionState::None) {
        m.extraction = morphic::ExtractionState::Candidate;
      }
    } else if (intent.attribution.dist_px < thresholds_.release_dist_px) {
      ++m.sustained_release_ticks;
      if (m.extraction == morphic::ExtractionState::Candidate &&
          m.sustained_release_ticks >= thresholds_.sustained_ticks) {
        m.extraction = morphic::ExtractionState::None;  // decayed back
        m.sustained_divergence_ticks = 0;
      }
    }

    if (intent.over_threshold && !m.fracture_queued && on_request_fracture_) {
      // QUEUE the fracture — the epoch applies it at the tick boundary (member
      // is NOT erased here). Freeze: stop rescoring this member.
      m.fracture_queued = true;
      forensic::Log(
          "EXTRACTION",
          "queue leader=" + IdOf(leader_) + " member=" + IdOf(m.surface) +
              " reason=" + std::string(morphic::ToString(intent.attribution.dominant)) +
              " score=" + std::to_string(intent.attribution.score) +
              " dist=" + std::to_string(static_cast<int>(intent.attribution.dist_px)) +
              " vel=" + std::to_string(static_cast<int>(intent.attribution.velocity_px_s)) +
              " dir=" + std::to_string(intent.attribution.directional_dot) +
              " sustained=" + std::to_string(intent.attribution.sustained_ticks));
      on_request_fracture_(m.surface, intent);
    }
  }
}

// ===========================================================================
// PHASE 8D — dock detection
// ===========================================================================
//
// The dragged leader (a single, detached surface) converging onto another
// detached surface forms a group. Inverse of extraction (membership ADD). This
// is a DISTINCT intentional gesture: deliberate proximity + overlap, sustained.
// We scan all surfaces, score each detached candidate, pick the best, and apply
// hysteresis so a fly-by never docks and boundary jitter never flickers the
// candidate. On sustained over-threshold we QUEUE a dock on the epoch (applied at
// the tick boundary with a re-validation); the surfaces are NOT grouped here.
void InteractionSession::DetectDocking() {
  // Guards (I-D4 / I-D5 / single depth): primary, single-member, Dragging, and the
  // dragged leader must itself be DETACHED. A grouped or multi-member drag never
  // docks (that would be regroup-into-group / nested topology — forbidden in 8D).
  if (is_derived_ || dock_queued_) return;
  if (mode_ != InteractionMode::Dragging || members_.size() != 1) return;
  if (leader_ == nullptr || model_ == nullptr || graph_ == nullptr) return;
  if (graph_->GroupOf(leader_) != SurfaceGraph::kNoGroup) return;  // not detached

  const RECT a = target_bounds_;  // the leader's current (live) rect

  // Find the best detached candidate target B (highest score).
  SurfaceShell* best = nullptr;
  morphic::DockingIntent best_intent{};
  for (SurfaceShell* s : model_->z_order()) {
    if (s == leader_ || s == nullptr || s->GetHandle() == nullptr) continue;
    if (graph_->GroupOf(s) != SurfaceGraph::kNoGroup) continue;  // target must be detached
    const RECT b = model_->bounds(s);
    // Score with the CURRENT sustained count for this target (so a stable pair
    // accumulates; switching targets resets via the tracking below).
    const int sustained =
        (s == dock_target_) ? dock_sustained_ticks_ : 0;
    const morphic::DockingIntent intent =
        morphic::EvaluateDock(a, b, sustained, dock_thresholds_);
    if (best == nullptr ||
        intent.attribution.score > best_intent.attribution.score) {
      best = s;
      best_intent = intent;
    }
  }

  if (best_intent.attribution.score > metrics_.peak_docking_score) {
    metrics_.peak_docking_score = best_intent.attribution.score;
  }

  // A candidate is "in range" when proximity + overlap gates clear (the hard
  // gates inside EvaluateDock minus the sustained requirement). We re-derive that
  // from the attribution so the sustained counter is what crosses the threshold.
  const bool in_range =
      best != nullptr &&
      best_intent.attribution.distance_px < dock_thresholds_.dock_distance_px &&
      best_intent.attribution.overlap_ratio > dock_thresholds_.overlap_ratio;

  if (in_range) {
    if (best != dock_target_) {
      // New/switched target — reset accumulation (boring, deterministic).
      dock_target_ = best;
      dock_sustained_ticks_ = 0;
      dock_release_ticks_ = 0;
      dock_state_ = morphic::DockingState::Candidate;
    }
    ++dock_sustained_ticks_;
    dock_release_ticks_ = 0;
    if (dock_state_ == morphic::DockingState::None) {
      dock_state_ = morphic::DockingState::Candidate;
    }
  } else {
    // Out of range — decay the candidate (hysteresis / anti-spam).
    if (dock_state_ == morphic::DockingState::Candidate) {
      ++dock_release_ticks_;
      if (dock_release_ticks_ >= dock_thresholds_.sustained_ticks) {
        dock_state_ = morphic::DockingState::None;
        dock_target_ = nullptr;
        dock_sustained_ticks_ = 0;
        dock_release_ticks_ = 0;
      }
    }
    return;
  }

  // Sustained over the threshold → queue the dock (one per drag).
  if (dock_sustained_ticks_ >= dock_thresholds_.sustained_ticks &&
      best_intent.attribution.score >= dock_thresholds_.score_threshold &&
      on_request_dock_) {
    dock_queued_ = true;
    dock_state_ = morphic::DockingState::Docked;
    forensic::Log(
        "DOCK",
        "queue source=" + IdOf(leader_) + " target=" + IdOf(best) +
            " score=" + std::to_string(best_intent.attribution.score) +
            " dist=" +
            std::to_string(static_cast<int>(best_intent.attribution.distance_px)) +
            " overlap=" + std::to_string(best_intent.attribution.overlap_ratio) +
            " sustained=" + std::to_string(dock_sustained_ticks_));
    // Re-evaluate with the final sustained count so the queued snapshot is honest.
    const morphic::DockingIntent snapshot = morphic::EvaluateDock(
        a, model_->bounds(best), dock_sustained_ticks_, dock_thresholds_);
    on_request_dock_(leader_, best, snapshot);
  }
}

RECT InteractionSession::RemoveMember(SurfaceShell* member) {
  RECT bounds = origin_bounds_leader_;
  for (auto it = members_.begin(); it != members_.end(); ++it) {
    if (it->surface == member) {
      bounds = MemberBoundsFor(*it, target_bounds_);  // current live geometry
      it->extraction = morphic::ExtractionState::Extracted;
      members_.erase(it);
      forensic::Log("SESSION", "RemoveMember id=" + IdOf(member) +
                                   " remaining=" + std::to_string(members_.size()));
      break;
    }
  }
  return bounds;
}
