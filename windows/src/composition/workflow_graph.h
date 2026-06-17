#pragma once

#include "../core/types.h"
#include "workspace_intent.h"
#include <unordered_map>
#include <vector>
#include <string>

namespace morphic {

// Phase 7D: Workflow Relationship Graphs.
//
// ARCHITECTURAL INVARIANT:
// Workflow relationships model HUMAN OPERATIONAL ASSOCIATION,
// NOT restore dependency. Restore dependencies are in Phase 5
// (SurfaceDependencyIntent). Do NOT duplicate them here.
//
// Good: "these surfaces belong to the same operational flow"
// Bad:  "this surface must restore before that one"
//
// This is an ADVISORY layer. WorkflowGraph NEVER governs:
//   - activation order
//   - topology mutation
//   - continuity truth
//   - scheduler priority

// How are two surfaces related in a human workflow?
enum class WorkflowRelationship {
    CoEditing,      // Working on the same content simultaneously
    InspectedBy,    // One surface inspects the state of another
    MonitoredBy,    // One surface monitors output of another
    DerivedFrom,    // Created from or based on another (e.g., split view)
    TemporaryCompanion, // Transient association, will dissolve
    SharedContext   // Surfaces share contextual information
};

inline const char* toString(WorkflowRelationship rel) {
    switch (rel) {
        case WorkflowRelationship::CoEditing:          return "CoEditing";
        case WorkflowRelationship::InspectedBy:        return "InspectedBy";
        case WorkflowRelationship::MonitoredBy:        return "MonitoredBy";
        case WorkflowRelationship::DerivedFrom:        return "DerivedFrom";
        case WorkflowRelationship::TemporaryCompanion: return "TemporaryCompanion";
        case WorkflowRelationship::SharedContext:      return "SharedContext";
    }
    return "Unknown";
}

// An edge in the workflow graph.
struct WorkflowEdge {
    NodeId from = kInvalidNodeId;
    NodeId to = kInvalidNodeId;
    WorkflowRelationship relationship = WorkflowRelationship::SharedContext;
    SemanticConfidence confidence = SemanticConfidence::Explicit;
};

// Phase 7D: Workflow Graph.
//
// A DAG of human operational associations between surfaces.
// ADVISORY ONLY — provides context for:
//   - "which surfaces belong to the same task?"
//   - "which workflows are active?"
//   - "which surfaces should be surfaced together?"
//
// Consumed by: AdaptiveOrchestrator (7E), SessionContinuity (7C).
// NEVER consumed by runtime kernel for correctness.
class WorkflowGraph {
public:
    WorkflowGraph() = default;

    void addRelationship(NodeId from, NodeId to,
                         WorkflowRelationship relationship,
                         SemanticConfidence confidence = SemanticConfidence::Explicit) {
        WorkflowEdge edge;
        edge.from = from;
        edge.to = to;
        edge.relationship = relationship;
        edge.confidence = confidence;
        edges_.push_back(edge);
    }

    // Remove all edges involving this surface.
    void removeSurface(NodeId surfaceId) {
        edges_.erase(
            std::remove_if(edges_.begin(), edges_.end(),
                [surfaceId](const WorkflowEdge& e) {
                    return e.from == surfaceId || e.to == surfaceId;
                }),
            edges_.end());
    }

    // Advisory: which surfaces are in the same workflow as this one?
    std::vector<NodeId> workflowPeers(NodeId surfaceId) const {
        std::vector<NodeId> peers;
        for (const auto& edge : edges_) {
            if (edge.from == surfaceId && edge.to != surfaceId) {
                peers.push_back(edge.to);
            }
            if (edge.to == surfaceId && edge.from != surfaceId) {
                peers.push_back(edge.from);
            }
        }
        return peers;
    }

    // Advisory: all relationships for a surface.
    std::vector<WorkflowEdge> relationshipsFor(NodeId surfaceId) const {
        std::vector<WorkflowEdge> result;
        for (const auto& edge : edges_) {
            if (edge.from == surfaceId || edge.to == surfaceId) {
                result.push_back(edge);
            }
        }
        return result;
    }

    // How many distinct workflows exist? (connected component count)
    // This is an approximation — useful for diagnostics.
    int workflowCount() const {
        if (edges_.empty()) return 0;

        std::unordered_map<NodeId, NodeId> parent;

        auto find = [&](NodeId n) -> NodeId {
            while (parent.count(n) && parent[n] != n) n = parent[n];
            return n;
        };

        for (const auto& edge : edges_) {
            if (!parent.count(edge.from)) parent[edge.from] = edge.from;
            if (!parent.count(edge.to)) parent[edge.to] = edge.to;
            parent[find(edge.from)] = find(edge.to);
        }

        std::unordered_map<NodeId, bool> roots;
        for (const auto& [node, _] : parent) {
            roots[find(node)] = true;
        }
        return static_cast<int>(roots.size());
    }

    size_t edgeCount() const { return edges_.size(); }

    const std::vector<WorkflowEdge>& allEdges() const { return edges_; }

private:
    std::vector<WorkflowEdge> edges_;
};

} // namespace morphic
