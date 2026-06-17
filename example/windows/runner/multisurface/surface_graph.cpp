#include "multisurface/surface_graph.h"

#include <algorithm>
#include <string>

#include "forensic_trace.h"
#include "runtime_events.h"
#include "surface_shell.h"

namespace {

// Exclusive relationships: a dependent can hold at most one of these at a time.
// Attach() replaces any prior exclusive edge.
bool IsExclusive(SurfaceRelationship r) {
  switch (r) {
    case SurfaceRelationship::Parent:
    case SurfaceRelationship::DockHost:
    case SurfaceRelationship::ExtractionTether:
    case SurfaceRelationship::Transient:
      return true;
    default:
      return false;
  }
}

std::string IdOf(SurfaceShell* s) {
  return s ? s->id() : std::string("<none>");
}

}  // namespace

SurfaceGraph::SurfaceGraph(EventBus* bus) : bus_(bus) {
  if (bus_) {
    bus_token_ = bus_->Subscribe([this](RuntimeEvent event, SurfaceShell* s) {
      if (event == RuntimeEvent::SurfaceCreated) {
        OnSurfaceCreated(s);
      } else if (event == RuntimeEvent::SurfaceDestroyed) {
        OnSurfaceDestroyed(s);
      }
    });
  }
}

SurfaceGraph::~SurfaceGraph() {
  // I-A2 closure: yank our subscription so Publish never re-enters a freed
  // `this` capture if the bus outlives us in destruction order.
  if (bus_ && bus_token_ != 0) bus_->Unsubscribe(bus_token_);
}

std::vector<std::string> SurfaceGraph::CheckInvariants() const {
  std::vector<std::string> failures;

  // T1: group_of_ entries point to a real group.
  for (const auto& [surface, gid] : group_of_) {
    if (groups_.find(gid) == groups_.end()) {
      failures.push_back("T1: surface " + IdOf(surface) + " group_of_=" +
                          std::to_string(gid) + " missing from groups_");
    }
  }

  // T2: bidirectional consistency — every member-of-a-group has a back-edge.
  for (const auto& [gid, members] : groups_) {
    for (SurfaceShell* s : members) {
      auto it = group_of_.find(s);
      if (it == group_of_.end()) {
        failures.push_back("T2: surface " + IdOf(s) + " in group_" +
                            std::to_string(gid) +
                            " but not in group_of_ (missing back-edge)");
      } else if (it->second != gid) {
        failures.push_back("T2: surface " + IdOf(s) + " in group_" +
                            std::to_string(gid) + " but group_of_ says " +
                            std::to_string(it->second));
      }
    }
    // T3: groups shouldn't contain only one member (Ungroup auto-dissolves).
    if (members.size() < 2) {
      failures.push_back("T3: group_" + std::to_string(gid) +
                          " has " + std::to_string(members.size()) +
                          " members (singletons should auto-dissolve)");
    }
  }

  // T4: every edge has both anchor and dependent populated.
  for (const Edge& e : edges_) {
    if (e.anchor == nullptr) {
      failures.push_back("T4: edge has null anchor (dependent=" +
                          IdOf(e.dependent) + ")");
    }
    if (e.dependent == nullptr) {
      failures.push_back("T4: edge has null dependent (anchor=" +
                          IdOf(e.anchor) + ")");
    }
  }

  return failures;
}

void SurfaceGraph::OnSurfaceCreated(SurfaceShell* /*surface*/) {
  // Registration is implicit — surfaces don't need pre-registration to be
  // attached. Hook exists so future phases (8D dock catalog) can record them.
}

void SurfaceGraph::OnSurfaceDestroyed(SurfaceShell* surface) {
  if (surface == nullptr) return;
  // Yank from any group first so MembersOf returns coherent state during the
  // edge-drop's event publishing.
  Ungroup(surface);
  DropEdgesTouching(surface);
}

void SurfaceGraph::Attach(SurfaceShell* anchor, SurfaceShell* dependent,
                          SurfaceRelationship relation) {
  if (anchor == nullptr || dependent == nullptr || anchor == dependent) {
    return;
  }
  if (relation == SurfaceRelationship::None ||
      relation == SurfaceRelationship::Detached) {
    return;  // pseudo-relations, not valid for Attach
  }
  // If dependent already has an exclusive edge, detach it first so observers
  // see a clean Detached → Attached sequence rather than a silent replacement.
  if (IsExclusive(relation)) {
    Detach(dependent);
  }
  edges_.push_back({anchor, dependent, relation});
  forensic::Log("SURFACE_GRAPH",
                "Attach " + IdOf(anchor) + " <-[" + ToString(relation) +
                    "]- " + IdOf(dependent) +
                    " (edges=" + std::to_string(edges_.size()) + ")");
  forensic::Log("RELATIONSHIP_MUTATION",
                "anchor=" + IdOf(anchor) + " dependent=" + IdOf(dependent) +
                    " relation=" + ToString(relation));
  if (bus_) {
    bus_->Publish(RuntimeEvent::SurfaceAttached, dependent);
  }
}

void SurfaceGraph::Detach(SurfaceShell* dependent) {
  if (dependent == nullptr) return;
  // Detach all exclusive edges where this surface is the DEPENDENT (these are
  // the ones a fresh Attach would replace). Non-exclusive relationships
  // (Overlay, ToolPalette, Grouped via group set) survive and must be removed
  // explicitly.
  bool removed = false;
  edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                              [dependent, &removed](const Edge& e) {
                                if (e.dependent == dependent &&
                                    IsExclusive(e.relation)) {
                                  removed = true;
                                  return true;
                                }
                                return false;
                              }),
               edges_.end());
  if (!removed) return;
  forensic::Log("SURFACE_GRAPH",
                "Detach dependent=" + IdOf(dependent) +
                    " (edges=" + std::to_string(edges_.size()) + ")");
  forensic::Log("RELATIONSHIP_MUTATION",
                "dependent=" + IdOf(dependent) + " relation=Detached");
  if (bus_) {
    bus_->Publish(RuntimeEvent::SurfaceDetached, dependent);
  }
}

SurfaceGraph::GroupId SurfaceGraph::Group(
    const std::vector<SurfaceShell*>& members) {
  if (members.size() < 2) return kNoGroup;  // singleton isn't a group
  for (SurfaceShell* s : members) {
    if (s == nullptr || group_of_.count(s)) {
      // One member is already in a group — reject the whole call rather than
      // silently partial-grouping. Caller must Ungroup first.
      forensic::Log("SURFACE_GRAPH WARN",
                    "Group rejected: member already grouped id=" + IdOf(s));
      return kNoGroup;
    }
  }
  // PHASE 10.5 Fix 2 — product-policy groupability enforcement at the SINGLE topology
  // chokepoint. The predicate is OPAQUE (no SurfaceKind); its implementation lives in
  // the wiring seam (morphic_runtime.cpp) and consults SurfacePolicy::IsGroupable. If
  // any member pair is denied, reject the whole group. The docking path self-heals:
  // OnDock routes through here and already has a "Group failed" rejection branch
  // (interaction_router.cpp). Unset predicate = allow all (pre-wiring / rollback).
  if (can_group_) {
    for (size_t i = 0; i < members.size(); ++i) {
      for (size_t j = i + 1; j < members.size(); ++j) {
        if (!can_group_(members[i], members[j])) {
          forensic::Log("SURFACE_GRAPH WARN",
                        "Group rejected by policy: " + IdOf(members[i]) + " + " +
                            IdOf(members[j]) + " not co-groupable");
          return kNoGroup;
        }
      }
    }
  }
  const GroupId id = next_group_id_++;
  groups_[id].reserve(members.size());
  std::string member_log;
  for (SurfaceShell* s : members) {
    groups_[id].insert(s);
    group_of_[s] = id;
    if (!member_log.empty()) member_log += ",";
    member_log += IdOf(s);
  }
  forensic::Log("SURFACE_GRAPH",
                "Group id=" + std::to_string(id) +
                    " members=[" + member_log + "]");
  if (bus_) {
    // Publish for one member — the group id is queryable via GroupOf if
    // consumers need the rest. Future event-bus extension can carry payloads.
    bus_->Publish(RuntimeEvent::SurfaceGrouped, members.front());
  }
  return id;
}

void SurfaceGraph::Ungroup(SurfaceShell* surface) {
  if (surface == nullptr) return;
  auto it = group_of_.find(surface);
  if (it == group_of_.end()) return;
  const GroupId id = it->second;
  group_of_.erase(it);
  auto& set = groups_[id];
  set.erase(surface);
  forensic::Log("SURFACE_GRAPH",
                "Ungroup id=" + std::to_string(id) +
                    " surface=" + IdOf(surface) +
                    " remaining=" + std::to_string(set.size()));
  if (bus_) {
    bus_->Publish(RuntimeEvent::SurfaceUngrouped, surface);
  }
  // Collapse singleton: a group with one member is meaningless. Notify the
  // straggler so observers can clean up coordinated state.
  if (set.size() == 1) {
    SurfaceShell* last = *set.begin();
    set.clear();
    groups_.erase(id);
    group_of_.erase(last);
    forensic::Log("SURFACE_GRAPH",
                  "Group id=" + std::to_string(id) +
                      " collapsed (singleton) — ungrouped " + IdOf(last));
    if (bus_) {
      bus_->Publish(RuntimeEvent::SurfaceUngrouped, last);
    }
  } else if (set.empty()) {
    groups_.erase(id);
  }
}

void SurfaceGraph::DissolveGroup(GroupId id) {
  auto it = groups_.find(id);
  if (it == groups_.end()) return;
  // Copy members so iteration is stable while we publish per-member events.
  std::vector<SurfaceShell*> members(it->second.begin(), it->second.end());
  for (SurfaceShell* s : members) {
    group_of_.erase(s);
    if (bus_) bus_->Publish(RuntimeEvent::SurfaceUngrouped, s);
  }
  groups_.erase(it);
  forensic::Log("SURFACE_GRAPH",
                "DissolveGroup id=" + std::to_string(id) +
                    " members=" + std::to_string(members.size()));
}

std::vector<SurfaceGraph::Edge> SurfaceGraph::EdgesOf(
    SurfaceShell* surface) const {
  std::vector<Edge> out;
  for (const Edge& e : edges_) {
    if (e.anchor == surface || e.dependent == surface) out.push_back(e);
  }
  return out;
}

SurfaceShell* SurfaceGraph::AnchorOf(SurfaceShell* dependent) const {
  for (const Edge& e : edges_) {
    if (e.dependent == dependent && IsExclusive(e.relation)) {
      return e.anchor;
    }
  }
  return nullptr;
}

SurfaceGraph::GroupId SurfaceGraph::GroupOf(SurfaceShell* surface) const {
  auto it = group_of_.find(surface);
  return it != group_of_.end() ? it->second : kNoGroup;
}

std::vector<SurfaceShell*> SurfaceGraph::MembersOf(GroupId id) const {
  auto it = groups_.find(id);
  if (it == groups_.end()) return {};
  return std::vector<SurfaceShell*>(it->second.begin(), it->second.end());
}

void SurfaceGraph::DropEdgesTouching(SurfaceShell* surface) {
  // No event publish here — the SurfaceDestroyed event already fires from the
  // manager. Edges leaving with the surface are implicit. Future phases could
  // publish per-edge Detached if observers need granular cleanup signals.
  const size_t before = edges_.size();
  edges_.erase(std::remove_if(edges_.begin(), edges_.end(),
                              [surface](const Edge& e) {
                                return e.anchor == surface ||
                                       e.dependent == surface;
                              }),
               edges_.end());
  if (edges_.size() != before) {
    forensic::Log("SURFACE_GRAPH",
                  "DropEdgesTouching id=" + IdOf(surface) + " removed=" +
                      std::to_string(before - edges_.size()));
  }
}
