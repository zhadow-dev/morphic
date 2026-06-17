#include "interaction_router.h"

#include <string>
#include <utility>
#include <vector>

#include "forensic_trace.h"
#include "multisurface/interaction_epoch.h"
#include "multisurface/surface_graph.h"
#include "multisurface/surface_relationship.h"
#include "runtime_events.h"
#include "surface_model.h"
#include "surface_shell.h"

InteractionRouter::InteractionRouter(SurfaceModel* model, EventBus* bus,
                                     FrameClock* clock, SurfaceGraph* graph,
                                     PresentationCoordinator* coordinator)
    : model_(model), bus_(bus), clock_(clock), graph_(graph),
      coordinator_(coordinator) {}

InteractionRouter::~InteractionRouter() {
  // If an epoch is still alive at router destruction, the InteractionEpoch
  // destructor handles emergency teardown (commits the transaction, unsubscribes
  // from clock). No need to do anything explicit here.
  DestroyEpoch();
}

void InteractionRouter::RequestActivate(SurfaceShell* surface) {
  if (surface == nullptr || model_ == nullptr) return;
  forensic::Log("ROUTER",
                "RequestActivate id=" + surface->id() +
                    " (compose Activate + Raise + Focus)");
  model_->Activate(surface);
  model_->Raise(surface);
  model_->Focus(surface);
}

void InteractionRouter::BeginDrag(SurfaceShell* surface, POINT pointer_screen) {
  if (surface == nullptr || epoch_) return;

  // PHASE 8B — multi-member expansion. If surface is in a group, the epoch's
  // primary session includes ALL group members (leader-first by convention).
  std::vector<SurfaceShell*> members{surface};
  if (graph_) {
    const SurfaceGraph::GroupId gid = graph_->GroupOf(surface);
    if (gid != SurfaceGraph::kNoGroup) {
      for (SurfaceShell* m : graph_->MembersOf(gid)) {
        if (m != surface) members.push_back(m);
      }
      forensic::Log("ROUTER",
                    "BeginDrag id=" + surface->id() + " group=" +
                        std::to_string(gid) +
                        " expanded_to=" + std::to_string(members.size()) +
                        " members");
    }
  }

  // PHASE 8C/8D — the epoch owns the transaction + clock + sessions. Its fracture
  // observer is OnFracture (graph REMOVE + events); its dock observer is OnDock
  // (graph ADD + events). The epoch spawns the derived session itself.
  epoch_ = std::make_unique<InteractionEpoch>(
      model_, clock_, bus_, graph_, coordinator_, InteractionMode::Dragging,
      surface, std::move(members), pointer_screen, 0, thresholds_,
      dock_thresholds_,
      [this](SurfaceShell* m, const morphic::ExtractionIntent& intent) {
        OnFracture(m, intent);
      },
      [this](SurfaceShell* src, SurfaceShell* tgt) { OnDock(src, tgt); });

  SetCapture(surface->GetHandle());
  forensic::Log("ROUTER", "SetCapture id=" + surface->id());
}

void InteractionRouter::BeginResize(SurfaceShell* surface,
                                    POINT pointer_screen, int resize_edge) {
  if (surface == nullptr || epoch_) return;

  // Resize epochs are ALWAYS single-member (no group resize, no extraction, and
  // no docking — I-D4: docking cannot occur during resize).
  std::vector<SurfaceShell*> members{surface};
  epoch_ = std::make_unique<InteractionEpoch>(
      model_, clock_, bus_, graph_, coordinator_, InteractionMode::Resizing,
      surface, std::move(members), pointer_screen, resize_edge, thresholds_,
      dock_thresholds_,
      /*on_fracture=*/nullptr,   // resize never fractures
      /*on_dock=*/nullptr);      // resize never docks (I-D4)

  SetCapture(surface->GetHandle());
  forensic::Log("ROUTER", "SetCapture id=" + surface->id());
}

void InteractionRouter::UpdatePointer(POINT pointer_screen) {
  if (epoch_) epoch_->UpdatePointer(pointer_screen);
}

void InteractionRouter::EndInteraction() {
  if (!epoch_) return;

  SurfaceShell* leader = epoch_->leader();

  // PRE-RESET the epoch ptr before ReleaseCapture so the synchronous
  // WM_CAPTURECHANGED handler finds an idle router and bails out of
  // CancelInteraction (7E rev4 proactive-terminator pattern).
  std::unique_ptr<InteractionEpoch> dying = std::move(epoch_);

  if (leader && GetCapture() == leader->GetHandle()) {
    ReleaseCapture();
    forensic::Log("ROUTER", "ReleaseCapture id=" + leader->id());
  }

  dying->End();  // ends all sessions + commits the ONE transaction once
  last_metrics_ = dying->last_metrics();
}

void InteractionRouter::CancelInteraction(SurfaceShell* surface,
                                          const char* reason) {
  if (!epoch_) return;
  if (surface != nullptr && surface != epoch_->leader()) return;

  SurfaceShell* leader = epoch_->leader();
  std::unique_ptr<InteractionEpoch> dying = std::move(epoch_);

  if (leader && GetCapture() == leader->GetHandle()) {
    ReleaseCapture();
    forensic::Log("ROUTER", "ReleaseCapture (cancel) id=" + leader->id());
  }

  dying->Cancel(reason);
  last_metrics_ = dying->last_metrics();
}

void InteractionRouter::OnFracture(SurfaceShell* member,
                                   const morphic::ExtractionIntent& intent) {
  if (member == nullptr || epoch_ == nullptr) return;
  SurfaceShell* leader = epoch_->leader();

  // EVENT ORDERING (I-X15): TopologyMutated is a PRE-state event — fire it BEFORE
  // the graph mutation so observers still see the pre-fracture topology.
  if (bus_) bus_->Publish(RuntimeEvent::TopologyMutated, member);

  // Mutate topology truth: leave the group (Detached/regroup-eligible), and set
  // a soft ExtractionTether to the leader for the duration of the drag.
  if (graph_) {
    graph_->Ungroup(member);
    graph_->Attach(leader, member, SurfaceRelationship::ExtractionTether);
  }

  // POST-state events.
  if (bus_) {
    bus_->Publish(RuntimeEvent::GroupFractured, leader);
    bus_->Publish(RuntimeEvent::SurfaceDetached, member);
  }

  forensic::Log("ROUTER",
                "OnFracture leader=" + (leader ? leader->id() : std::string("?")) +
                    " member=" + member->id() + " score=" +
                    std::to_string(intent.attribution.score));
  // The epoch spawns the derived session itself (it owns session lifetime).
  // The ExtractionTether → Detached resolution happens at epoch End.
}

void InteractionRouter::OnDock(SurfaceShell* source, SurfaceShell* target) {
  if (source == nullptr || target == nullptr || graph_ == nullptr) return;

  // EVENT ORDERING (mirrors I-X15): TopologyMutated is a PRE-state event — fire it
  // BEFORE the graph mutation so observers still see the pre-dock topology
  // (payload = the dragged source).
  if (bus_) bus_->Publish(RuntimeEvent::TopologyMutated, source);

  // Mutate topology truth: form the group. Group() rejects already-grouped members
  // (flat-topology guard, I-D2/I-D5); the epoch already re-validated both detached,
  // but guard defensively.
  const SurfaceGraph::GroupId gid =
      graph_->Group(std::vector<SurfaceShell*>{source, target});
  if (gid == SurfaceGraph::kNoGroup) {
    forensic::Log("ROUTER", "OnDock REJECTED (Group failed) source=" +
                                source->id() + " target=" + target->id());
    return;
  }

  // POST-state event. The current drag is NOT restructured — both surfaces stay
  // independent for the rest of this drag; the rigid grouped co-move begins on the
  // NEXT drag of either surface (8B behavior). Capture never transfers (I-D3).
  if (bus_) bus_->Publish(RuntimeEvent::SurfaceDocked, source);

  forensic::Log("ROUTER", "OnDock source=" + source->id() + " target=" +
                              target->id() + " group=" + std::to_string(gid));
}

void InteractionRouter::OnDpiChanged(SurfaceShell* surface,
                                     const RECT& new_bounds) {
  if (!epoch_ || epoch_->leader() != surface) return;
  epoch_->OnDpiChangedLeader(new_bounds);
}

void InteractionRouter::DestroyEpoch() {
  if (epoch_) {
    last_metrics_ = epoch_->last_metrics();
    epoch_.reset();  // epoch dtor commits the transaction if not already
  }
}

InteractionMode InteractionRouter::mode() const {
  return epoch_ ? epoch_->mode() : InteractionMode::None;
}

SurfaceShell* InteractionRouter::interacting() const {
  return epoch_ ? epoch_->leader() : nullptr;
}

bool InteractionRouter::subscribed_to_clock() const {
  return epoch_ && epoch_->subscribed_to_clock();
}

RECT InteractionRouter::target_bounds() const {
  return epoch_ ? epoch_->target_bounds() : empty_rect_;
}

std::optional<RECT> InteractionRouter::presented_bounds() const {
  return epoch_ ? epoch_->presented_bounds() : std::nullopt;
}

InteractionRouter::Velocity InteractionRouter::current_velocity() const {
  return epoch_ ? epoch_->current_velocity() : Velocity{};
}

size_t InteractionRouter::derived_count() const {
  return epoch_ ? epoch_->derived_count() : 0;
}
