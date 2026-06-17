#ifndef RUNNER_MULTISURFACE_SURFACE_GRAPH_H_
#define RUNNER_MULTISURFACE_SURFACE_GRAPH_H_

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "multisurface/surface_relationship.h"
#include "runtime_events.h"  // EventBus::Token

class EventBus;
class SurfaceShell;

// PHASE 8A — SurfaceGraph
//
// THE semantic topology authority for surface relationships. Owns:
//   - directed edges (parent → child) labelled with SurfaceRelationship
//   - undirected group sets (each surface ∈ at most one group)
//   - lifecycle hooks: surfaces auto-leave the graph when destroyed
//
// 8A explicitly DOES NOT:
//   - drive geometry (8B InteractionSession does — grouped drag/resize)
//   - enforce constraints (8B + 8D)
//   - implement docking acceptance rules (8D)
//   - implement extraction thresholds (8C)
//   - mutate native Win32 topology (NEVER — SetParent etc. stay untouched)
//
// All mutations publish RuntimeEvents (SurfaceAttached/Detached/Grouped/
// Ungrouped) so the orchestration debugger and future consumers observe a
// single source of truth for topology changes. Topology mutation is intended
// to occur OUTSIDE interaction transactions — performing it mid-interaction
// is legal but flagged by orchestration_debugger as a sharp-edge case.
class SurfaceGraph {
 public:
  using GroupId = std::uint32_t;
  static constexpr GroupId kNoGroup = 0;

  struct Edge {
    SurfaceShell* anchor;          // "parent" / dock host / extraction anchor
    SurfaceShell* dependent;       // "child" / docked / overlay / tether
    SurfaceRelationship relation;  // semantic kind
  };

  // PHASE 8B-prep — graph self-subscribes to the bus for lifecycle events
  // (SurfaceCreated/Destroyed). Token stored so the destructor unsubscribes
  // cleanly — closes I-A2.
  explicit SurfaceGraph(EventBus* bus);
  ~SurfaceGraph();

  SurfaceGraph(const SurfaceGraph&) = delete;
  SurfaceGraph& operator=(const SurfaceGraph&) = delete;

  // PHASE 10.5 Fix 2 — groupability enforcement seam. An OPAQUE predicate (NO
  // SurfaceKind — the runtime stays kind-agnostic) answering "may these two surfaces
  // co-group?". Injected by the wiring seam (morphic_runtime.cpp), where the
  // implementation consults SurfacePolicy::IsGroupable. Unset = allow all (pre-wiring
  // / rollback). Consulted at the single chokepoint, Group() — so docking, which
  // routes through Group(), inherits the enforcement for free.
  using GroupabilityPredicate =
      std::function<bool(SurfaceShell*, SurfaceShell*)>;
  void SetGroupabilityPredicate(GroupabilityPredicate predicate) {
    can_group_ = std::move(predicate);
  }

  // Lifecycle.
  // OnSurfaceCreated registers a surface so it can participate. OnSurfaceDestroyed
  // removes the surface from all edges and groups (auto-cleanup so callers
  // can't leak dangling pointers via the graph).
  void OnSurfaceCreated(SurfaceShell* surface);
  void OnSurfaceDestroyed(SurfaceShell* surface);

  // --- Relationships (directed edges) ---
  // Attach `dependent` to `anchor` with `relation`. If `dependent` already has
  // an anchor of an EXCLUSIVE kind (Parent/DockHost/ExtractionTether/Transient),
  // the prior edge is implicitly detached first and a Detached event fires
  // before the new attach (so observers see the topology mutation in order).
  void Attach(SurfaceShell* anchor, SurfaceShell* dependent,
              SurfaceRelationship relation);

  // Detach `dependent` from its current anchor (if any). No-op if not attached.
  // Publishes SurfaceDetached so observers see the relationship loss even when
  // no new anchor is set.
  void Detach(SurfaceShell* dependent);

  // --- Groups (undirected sets) ---
  // Returns the new group id (>0) or kNoGroup on failure. All members must be
  // currently un-grouped (a surface belongs to ≤1 group). Groups don't have
  // anchors — group transactions in 8B treat all members symmetrically.
  GroupId Group(const std::vector<SurfaceShell*>& members);

  // Removes one surface from its group. If the group drops to 1 member, the
  // remaining member is also un-grouped (singletons are not groups).
  void Ungroup(SurfaceShell* surface);

  // Dissolve a group entirely.
  void DissolveGroup(GroupId id);

  // --- Queries ---
  // Returns all edges where `surface` is the anchor OR the dependent.
  std::vector<Edge> EdgesOf(SurfaceShell* surface) const;

  // Returns the (single) anchor of an exclusive relationship for `dependent`,
  // or nullptr if none / non-exclusive. Useful for "who am I docked to?"
  SurfaceShell* AnchorOf(SurfaceShell* dependent) const;

  // Returns the group id of `surface`, or kNoGroup if ungrouped.
  GroupId GroupOf(SurfaceShell* surface) const;

  // All members of `id`, empty vector if no such group.
  std::vector<SurfaceShell*> MembersOf(GroupId id) const;

  size_t edge_count() const { return edges_.size(); }
  size_t group_count() const { return groups_.size(); }

  // PHASE 8B.5 — graph invariant audit. Returns empty vector when consistent;
  // each entry is a one-line description of a violation. Used by
  // IntegrityAuditor to catch topology corruption from chaos scenarios.
  // Checks:
  //   T1 — every group_of_ entry's GroupId exists in groups_
  //   T2 — every member s in groups_[g] has group_of_[s] == g (bidirectional)
  //   T3 — no group is singleton (we enforce dissolution at size==1)
  //   T4 — no edge has null anchor or null dependent
  std::vector<std::string> CheckInvariants() const;

 private:
  // Drop every edge that touches `surface` (called by OnSurfaceDestroyed and
  // implicitly by Attach when replacing an exclusive edge).
  void DropEdgesTouching(SurfaceShell* surface);

  EventBus* bus_;
  EventBus::Token bus_token_ = 0;
  std::vector<Edge> edges_;  // flat list — N is small (<100 typical)
  std::unordered_map<SurfaceShell*, GroupId> group_of_;
  std::unordered_map<GroupId, std::unordered_set<SurfaceShell*>> groups_;
  GroupId next_group_id_ = 1;
  GroupabilityPredicate can_group_;  // PHASE 10.5 Fix 2 — opaque policy gate (unset = allow)
};

#endif  // RUNNER_MULTISURFACE_SURFACE_GRAPH_H_
