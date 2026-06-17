#include "multisurface/interaction_epoch.h"

#include <algorithm>
#include <string>
#include <utility>

#include "forensic_trace.h"
#include "multisurface/surface_graph.h"
#include "runtime_events.h"
#include "surface_model.h"
#include "surface_shell.h"

uint64_t InteractionEpoch::next_id_ = 1;

namespace {
std::string IdOf(SurfaceShell* s) { return s ? s->id() : std::string("<none>"); }
}  // namespace

InteractionEpoch::InteractionEpoch(
    SurfaceModel* model, FrameClock* clock, EventBus* bus, SurfaceGraph* graph,
    PresentationCoordinator* coordinator, InteractionMode mode,
    SurfaceShell* leader, std::vector<SurfaceShell*> members,
    POINT pointer_origin, int resize_edge,
    const morphic::ExtractionThresholds& thresholds,
    const morphic::DockingThresholds& dock_thresholds,
    FractureObserver on_fracture, DockObserver on_dock)
    : model_(model), clock_(clock), bus_(bus), graph_(graph),
      coordinator_(coordinator), thresholds_(thresholds),
      dock_thresholds_(dock_thresholds),
      on_fracture_(std::move(on_fracture)), on_dock_(std::move(on_dock)),
      id_(next_id_++) {
  forensic::Log("EPOCH", "Begin id=" + std::to_string(id_) +
                             " leader=" + IdOf(leader) +
                             " members=" + std::to_string(members.size()));

  // PHASE 8C — the epoch owns the ONE transaction for the whole interaction.
  if (model_) model_->BeginTransaction();

  // The primary session. It does NOT own a transaction/subscription; it requests
  // fractures + docks via the callbacks below (queued, applied at the tick
  // boundary). graph_ is read-only (detached check for dock detection).
  primary_ = std::make_unique<InteractionSession>(
      model_, bus_, graph_, coordinator_, mode, leader, std::move(members),
      pointer_origin, resize_edge, /*is_derived=*/false, /*parent=*/nullptr,
      thresholds_, dock_thresholds_,
      [this](SurfaceShell* m, const morphic::ExtractionIntent& intent) {
        RequestFracture(m, intent);
      },
      [this](SurfaceShell* src, SurfaceShell* tgt,
             const morphic::DockingIntent& intent) {
        RequestDock(src, tgt, intent);
      });

  // The epoch owns the ONE clock subscription. Its tick drives every session.
  if (clock_) {
    clock_token_ = clock_->Subscribe([this](double dt_ms) { OnTick(dt_ms); });
  }
}

InteractionEpoch::~InteractionEpoch() {
  // Defensive: if neither End nor Cancel ran (router destroyed mid-interaction),
  // unsubscribe + commit the one transaction so we don't leave it open.
  if (clock_ && clock_token_ != 0) {
    clock_->Unsubscribe(clock_token_);
    clock_token_ = 0;
  }
  if (!committed_) {
    forensic::Log("EPOCH", "destructor without End/Cancel id=" +
                               std::to_string(id_) + " — emergency commit");
    if (model_) model_->CommitTransaction();
    committed_ = true;
  }
}

void InteractionEpoch::UpdatePointer(POINT pointer_screen) {
  if (primary_) primary_->UpdatePointer(pointer_screen);
  for (auto& d : derived_) {
    if (d) d->UpdatePointer(pointer_screen);
  }
}

void InteractionEpoch::RequestFracture(SurfaceShell* member,
                                       const morphic::ExtractionIntent& intent) {
  if (member == nullptr) return;
  if (applying_) return;  // no enqueue while applying (avoid recursion)
  // At most one pending fracture per surface per epoch.
  for (const auto& p : pending_) {
    if (p.member == member) return;
  }
  pending_.push_back(PendingFracture{member, intent});  // immutable snapshot
}

void InteractionEpoch::RequestDock(SurfaceShell* source, SurfaceShell* target,
                                   const morphic::DockingIntent& intent) {
  if (source == nullptr || target == nullptr || source == target) return;
  if (applying_) return;  // no enqueue while applying
  // At most one pending dock per (source,target) pair per epoch.
  for (const auto& d : pending_docks_) {
    if (d.source == source && d.target == target) return;
  }
  pending_docks_.push_back(PendingDock{source, target, intent});  // immutable
}

void InteractionEpoch::OnTick(double dt_ms) {
  if (committed_) return;  // defensive: never tick a committed epoch
  ++tick_;
  // SACRED: topology mutation applies ONLY here, at the tick boundary — never
  // inside a session's projection loop. Drive projection first (primary, then
  // each derived = primary-precedence), THEN apply queued fractures, THEN docks,
  // THEN reap. Fractures before docks: a member could fracture out AND a dock
  // resolve in the same boundary; applying fractures first keeps the topology
  // truth current for the dock re-validation.
  if (primary_) primary_->OnFrameTick(dt_ms);
  for (auto& d : derived_) {
    if (d) d->OnFrameTick(dt_ms);
  }
  ApplyPendingFractures();
  ApplyPendingDocks();
  ReapEmptySessions();
}

void InteractionEpoch::ApplyPendingFractures() {
  if (pending_.empty()) return;

  // Deterministic order: primary precedence is implicit (only the primary
  // queues), so order by extraction score desc, then by stable HWND identity —
  // so simultaneous (same-tick) fractures apply identically across runs.
  std::sort(pending_.begin(), pending_.end(),
            [](const PendingFracture& a, const PendingFracture& b) {
              if (a.intent_snapshot.attribution.score !=
                  b.intent_snapshot.attribution.score) {
                return a.intent_snapshot.attribution.score >
                       b.intent_snapshot.attribution.score;
              }
              return a.member->GetHandle() < b.member->GetHandle();
            });

  applying_ = true;
  std::vector<PendingFracture> batch;
  batch.swap(pending_);

  for (const PendingFracture& pf : batch) {
    if (primary_ == nullptr) break;

    // 1. Router observer drives the PRE-state event + graph mutation + tether +
    //    POST-state events (TopologyMutated → Ungroup → ExtractionTether →
    //    GroupFractured/SurfaceDetached). Topology truth changes here.
    if (on_fracture_) on_fracture_(pf.member, pf.intent_snapshot);

    // 2. Remove the member from the primary — returns its CURRENT bounds at this
    //    boundary (baseline freeze: the derived session starts from exactly the
    //    surface's pre-fracture position so there is NO geometry jump).
    const RECT freeze_bounds = primary_->RemoveMember(pf.member);

    // 3. Spawn the derived session in THIS epoch (no new transaction/subscription).
    //    Baseline = freeze_bounds (via the model's current bounds, which equals
    //    freeze_bounds since SetBounds already projected it); pointer_origin =
    //    current cursor so the derived delta starts at 0 → first projection ==
    //    pre-fracture position (semantic continuity).
    POINT cursor{};
    GetCursorPos(&cursor);
    if (model_) model_->SetBounds(pf.member, freeze_bounds);  // ensure model truth

    auto derived = std::make_unique<InteractionSession>(
        model_, bus_, graph_, coordinator_, InteractionMode::Dragging, pf.member,
        std::vector<SurfaceShell*>{pf.member}, cursor, /*resize_edge=*/0,
        /*is_derived=*/true, /*parent=*/primary_.get(), thresholds_,
        dock_thresholds_,
        /*on_request_fracture=*/nullptr,   // derived never extracts
        /*on_request_dock=*/nullptr);      // derived never docks (single depth)
    derived_.push_back(std::move(derived));

    ++last_metrics_.extractions;  // best-effort count surfaced in last_metrics
    forensic::Log("EPOCH", "fracture applied id=" + std::to_string(id_) +
                               " member=" + IdOf(pf.member) +
                               " derived_count=" + std::to_string(derived_.size()));
  }
  applying_ = false;
}

void InteractionEpoch::ApplyPendingDocks() {
  if (pending_docks_.empty()) return;

  // Deterministic order: stable HWND identity (source then target) so same-tick
  // docks apply identically across runs.
  std::sort(pending_docks_.begin(), pending_docks_.end(),
            [](const PendingDock& a, const PendingDock& b) {
              if (a.source->GetHandle() != b.source->GetHandle()) {
                return a.source->GetHandle() < b.source->GetHandle();
              }
              return a.target->GetHandle() < b.target->GetHandle();
            });

  applying_ = true;
  std::vector<PendingDock> batch;
  batch.swap(pending_docks_);

  for (const PendingDock& pd : batch) {
    SurfaceShell* src = pd.source;
    SurfaceShell* tgt = pd.target;
    if (src == nullptr || tgt == nullptr || src->GetHandle() == nullptr ||
        tgt->GetHandle() == nullptr) {
      continue;  // a participant died before the boundary
    }

    // RE-VALIDATE legality at the boundary (state may have changed since the
    // candidate was queued): both must still be DETACHED (ungrouped) and still in
    // range/overlap. A fracture or another dock earlier this boundary, or a
    // surface destroy, can invalidate a queued dock.
    if (graph_ == nullptr) continue;
    if (graph_->GroupOf(src) != SurfaceGraph::kNoGroup ||
        graph_->GroupOf(tgt) != SurfaceGraph::kNoGroup) {
      forensic::Log("EPOCH", "dock skipped (no longer detached) id=" +
                                 std::to_string(id_) + " src=" + IdOf(src) +
                                 " tgt=" + IdOf(tgt));
      continue;
    }
    if (model_) {
      const RECT a = model_->bounds(src);
      const RECT b = model_->bounds(tgt);
      const morphic::DockingIntent reval =
          morphic::EvaluateDock(a, b, dock_thresholds_.sustained_ticks,
                                dock_thresholds_);
      if (!reval.over_threshold) {
        forensic::Log("EPOCH", "dock skipped (no longer in range) id=" +
                                   std::to_string(id_) + " src=" + IdOf(src) +
                                   " tgt=" + IdOf(tgt));
        continue;
      }
    }

    // Apply: the router observer publishes TopologyMutated (pre) → graph Group →
    // SurfaceDocked (post). Topology truth changes here; the live session is NOT
    // restructured (rigid co-move begins on the NEXT drag).
    if (on_dock_) on_dock_(src, tgt);
    ++last_metrics_.docks;
    forensic::Log("EPOCH", "dock applied id=" + std::to_string(id_) +
                               " src=" + IdOf(src) + " tgt=" + IdOf(tgt) +
                               " score=" +
                               std::to_string(pd.intent_snapshot.attribution.score));
  }
  applying_ = false;
}

void InteractionEpoch::ReapEmptySessions() {
  // A session whose membership hit zero (all members extracted or destroy-pruned)
  // self-destructs at this boundary — no zombie sessions. The primary is never
  // reaped (it always retains its leader); only derived sessions can empty (their
  // single member could be destroy-pruned mid-drag).
  derived_.erase(
      std::remove_if(derived_.begin(), derived_.end(),
                     [](const std::unique_ptr<InteractionSession>& d) {
                       if (d && d->member_count() == 0) {
                         d->Cancel("zero-member-reap");
                         return true;
                       }
                       return false;
                     }),
      derived_.end());
}

void InteractionEpoch::Commit(bool snap_to_target, const char* reason) {
  if (committed_) return;

  // Unsubscribe FIRST so no further tick fires while we finalize.
  if (clock_ && clock_token_ != 0) {
    clock_->Unsubscribe(clock_token_);
    clock_token_ = 0;
  }

  // Cancel discards UNAPPLIED pending fractures (they never became truth).
  // Already-applied derived sessions keep their projected geometry.
  if (!snap_to_target) pending_.clear();

  // PHASE 8D — pending docks are ALWAYS discarded at commit (End OR Cancel).
  // Docking applies ONLY at a tick boundary (with re-validation); commit is not a
  // boundary, so a queued-but-unapplied dock must never be force-applied here —
  // that would bypass re-validation and the boundary law (I-D1). A dock that was
  // genuine would already have applied on a prior tick. This also gives
  // hostile_cancel_during_dock its guarantee: no partial topology mutation.
  pending_docks_.clear();

  // Finalize every session (each publishes its own Ended/SurfaceMoved; none of
  // them commit the transaction — that's the epoch's single job below).
  if (primary_) {
    if (snap_to_target) primary_->End();
    else primary_->Cancel(reason);
    last_metrics_ = primary_->metrics();  // primary metrics surface to the router
  }
  for (auto& d : derived_) {
    if (!d) continue;
    if (snap_to_target) d->End();
    else d->Cancel(reason);
  }

  // THE single commit for the whole interaction.
  if (model_) model_->CommitTransaction();
  committed_ = true;
  forensic::Log("EPOCH", "Commit id=" + std::to_string(id_) +
                             (snap_to_target ? " (end)" : " (cancel)") +
                             " derived=" + std::to_string(derived_.size()));
}

void InteractionEpoch::End() { Commit(/*snap_to_target=*/true, "end"); }
void InteractionEpoch::Cancel(const char* reason) {
  Commit(/*snap_to_target=*/false, reason);
}

void InteractionEpoch::OnDpiChangedLeader(const RECT& new_bounds) {
  if (primary_) primary_->OnDpiChangedLeader(new_bounds);
}

SurfaceShell* InteractionEpoch::leader() const {
  return primary_ ? primary_->leader() : nullptr;
}
InteractionMode InteractionEpoch::mode() const {
  return primary_ ? primary_->mode() : InteractionMode::None;
}
RECT InteractionEpoch::target_bounds() const {
  return primary_ ? primary_->target_bounds() : RECT{};
}
std::optional<RECT> InteractionEpoch::presented_bounds() const {
  return primary_ ? primary_->presented_bounds() : std::nullopt;
}
InteractionSession::Velocity InteractionEpoch::current_velocity() const {
  return primary_ ? primary_->current_velocity() : InteractionSession::Velocity{};
}
