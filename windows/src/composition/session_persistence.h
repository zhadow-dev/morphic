#pragma once

#include "../core/types.h"
#include "session_continuity.h"
#include "workspace_intent.h"
#include "workflow_graph.h"
#include "../experimental/attention_economics.h"
#include <string>
#include <vector>

namespace morphic {

// Phase 8D: Session Persistence.
//
// Real persistence for real continuity. Not theory — operational recovery.
//
// Key requirements:
//   - Schema versioning (mandatory for future migration)
//   - Corruption scoping (not monolithic recovery)
//   - Partial restoration modes (real systems need this)

// --- Schema versioning ---
constexpr uint32_t kSessionSchemaVersion = 1;

// --- Corruption scoping ---
// Recovery is NOT monolithic. Each corruption domain
// can be diagnosed and recovered independently.
enum class SessionCorruptionScope {
    None,                   // Clean
    WorkflowCorruption,     // Workflow edges reference missing surfaces
    TopologyCorruption,     // Workspace references invalid
    OrphanReferences,       // Surfaces exist in session but not in runtime
    InvalidLineage,         // SurfaceIdentity lineage mismatch
    MissingSurfaces,        // Session references surfaces that don't exist
    StaleWorkspaceIds,      // Workspace IDs from previous runtime
    IntentCorruption,       // Intent state references invalid workspaces
    AttentionCorruption     // Attention levels reference invalid surfaces
};

inline const char* toString(SessionCorruptionScope scope) {
    switch (scope) {
        case SessionCorruptionScope::None:                return "None";
        case SessionCorruptionScope::WorkflowCorruption:  return "WorkflowCorruption";
        case SessionCorruptionScope::TopologyCorruption:  return "TopologyCorruption";
        case SessionCorruptionScope::OrphanReferences:    return "OrphanReferences";
        case SessionCorruptionScope::InvalidLineage:      return "InvalidLineage";
        case SessionCorruptionScope::MissingSurfaces:     return "MissingSurfaces";
        case SessionCorruptionScope::StaleWorkspaceIds:   return "StaleWorkspaceIds";
        case SessionCorruptionScope::IntentCorruption:    return "IntentCorruption";
        case SessionCorruptionScope::AttentionCorruption: return "AttentionCorruption";
    }
    return "Unknown";
}

// --- Partial restoration modes ---
// Real systems cannot always restore everything.
enum class PartialRestoreMode {
    RestoreWhatIsAvailable,       // Best-effort: skip missing, don't fail
    RestoreMinimalSafe,           // Only ContinuityCritical workspaces
    RestoreWithoutDetached,       // Skip detached surfaces
    RestoreWithoutMissingRenderers, // Skip surfaces whose renderers are gone
    RestoreTopologyOnly           // Window positions only, no semantic state
};

// Corruption diagnosis for a persisted session.
struct SessionCorruptionReport {
    bool isClean = true;
    std::vector<SessionCorruptionScope> corruptedScopes;
    int orphanedSurfaceCount = 0;
    int staleWorkspaceCount = 0;
    int brokenWorkflowEdges = 0;
    std::string summary;
};

// Serializable session data.
struct PersistedSession {
    uint32_t schemaVersion = kSessionSchemaVersion;

    // Core session state
    SessionState sessionState;

    // Workspace intents
    struct PersistedWorkspaceIntent {
        uint64_t workspaceKey = 0;
        OperationalActivity activity = OperationalActivity::Editing;
        IntentDisposition disposition = IntentDisposition::Persistent;
        SemanticConfidence confidence = SemanticConfidence::Explicit;
        std::string label;
    };
    std::vector<PersistedWorkspaceIntent> workspaceIntents;

    // Workflow edges
    struct PersistedWorkflowEdge {
        uint64_t fromSurface = 0;
        uint64_t toSurface = 0;
        int relationship = 0; // WorkflowRelationship as int
        int confidence = 0;   // SemanticConfidence as int
    };
    std::vector<PersistedWorkflowEdge> workflowEdges;

    // Attention states
    struct PersistedAttention {
        uint64_t surfaceId = 0;
        int level = 0; // AttentionLevel as int
    };
    std::vector<PersistedAttention> attentionStates;
};

// Phase 8D: Session Persistence Manager.
//
// Handles serialization, deserialization, corruption detection,
// and partial restoration. This is where continuity becomes real.
class SessionPersistenceManager {
public:
    SessionPersistenceManager() = default;

    // --- Capture ---

    // Capture current runtime state into a persistable snapshot.
    PersistedSession captureSession(
        const SessionState& session,
        const WorkspaceIntentGraph& intents,
        const WorkflowGraph& workflows,
        const AttentionBudget& budget) const
    {
        PersistedSession persisted;
        persisted.schemaVersion = kSessionSchemaVersion;
        persisted.sessionState = session;

        // Capture workflow edges
        for (const auto& edge : workflows.allEdges()) {
            PersistedSession::PersistedWorkflowEdge pe;
            pe.fromSurface = edge.from;
            pe.toSurface = edge.to;
            pe.relationship = static_cast<int>(edge.relationship);
            pe.confidence = static_cast<int>(edge.confidence);
            persisted.workflowEdges.push_back(pe);
        }

        return persisted;
    }

    // --- Validate ---

    // Diagnose corruption in a persisted session before restoring.
    SessionCorruptionReport diagnoseCorruption(
        const PersistedSession& persisted) const
    {
        SessionCorruptionReport report;

        // Schema version check
        if (persisted.schemaVersion != kSessionSchemaVersion) {
            report.isClean = false;
            report.summary = "Schema version mismatch: expected " +
                std::to_string(kSessionSchemaVersion) + " got " +
                std::to_string(persisted.schemaVersion);
            return report;
        }

        // Check for orphaned workflow edges
        for (const auto& edge : persisted.workflowEdges) {
            if (edge.fromSurface == 0 || edge.toSurface == 0) {
                report.isClean = false;
                report.brokenWorkflowEdges++;
                if (std::find(report.corruptedScopes.begin(),
                              report.corruptedScopes.end(),
                              SessionCorruptionScope::WorkflowCorruption) ==
                    report.corruptedScopes.end()) {
                    report.corruptedScopes.push_back(
                        SessionCorruptionScope::WorkflowCorruption);
                }
            }
        }

        // Check for stale workspace references
        for (const auto& ws : persisted.workspaceIntents) {
            if (ws.workspaceKey == 0) {
                report.isClean = false;
                report.staleWorkspaceCount++;
                if (std::find(report.corruptedScopes.begin(),
                              report.corruptedScopes.end(),
                              SessionCorruptionScope::StaleWorkspaceIds) ==
                    report.corruptedScopes.end()) {
                    report.corruptedScopes.push_back(
                        SessionCorruptionScope::StaleWorkspaceIds);
                }
            }
        }

        if (report.isClean) {
            report.summary = "Clean";
        } else {
            report.summary = std::to_string(report.corruptedScopes.size()) +
                " corruption scope(s) detected";
        }

        return report;
    }

    // --- Restore ---

    // Determine what can be safely restored given corruption and mode.
    bool canRestore(const SessionCorruptionReport& corruption,
                    PartialRestoreMode mode) const
    {
        if (corruption.isClean) return true;

        switch (mode) {
            case PartialRestoreMode::RestoreWhatIsAvailable:
                return true; // Always attempt best-effort
            case PartialRestoreMode::RestoreMinimalSafe:
                return true; // Skip everything non-critical
            case PartialRestoreMode::RestoreTopologyOnly:
                return true; // Ignore semantic corruption
            case PartialRestoreMode::RestoreWithoutDetached:
            case PartialRestoreMode::RestoreWithoutMissingRenderers:
                // Only fail on topology corruption
                for (auto scope : corruption.corruptedScopes) {
                    if (scope == SessionCorruptionScope::TopologyCorruption) {
                        return false;
                    }
                }
                return true;
        }
        return false;
    }
};

} // namespace morphic
