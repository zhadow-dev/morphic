#pragma once

#include "../core/types.h"
#include "workspace_controller.h"
#include <unordered_map>
#include <string>

namespace morphic {

// Phase 7A: Workspace Intent Graph.
//
// ARCHITECTURAL INVARIANT:
// Human semantics are advisory context, NOT runtime authority.
// Intent NEVER overrides continuity truth.
// Intent NEVER governs activation, focus, or arbitration.
// Intent is consumed by: session continuity, attention economics,
// and adaptive orchestration — all advisory layers.

// --- Layer A: What is the workspace doing? ---
enum class OperationalActivity {
    Editing,        // Primary content creation/modification
    Inspecting,     // Examining state (debugger, inspector)
    Monitoring,     // Passive observation (logs, metrics, dashboards)
    Comparing,      // Side-by-side analysis
    Reviewing,      // Code review, document review
    Searching,      // Find/navigate/explore
    Debugging,      // Active debugging session
    Reference       // Read-only reference material
};

inline const char* toString(OperationalActivity activity) {
    switch (activity) {
        case OperationalActivity::Editing:    return "Editing";
        case OperationalActivity::Inspecting: return "Inspecting";
        case OperationalActivity::Monitoring: return "Monitoring";
        case OperationalActivity::Comparing:  return "Comparing";
        case OperationalActivity::Reviewing:  return "Reviewing";
        case OperationalActivity::Searching:  return "Searching";
        case OperationalActivity::Debugging:  return "Debugging";
        case OperationalActivity::Reference:  return "Reference";
    }
    return "Unknown";
}

// --- Layer B: How does this workspace behave operationally? ---
// THIS is what actually drives orchestration decisions.
// Two workspaces can both be "Editing" but one is Transient
// and the other is ContinuityCritical — completely different behavior.
enum class IntentDisposition {
    Persistent,           // Long-lived, restore across sessions
    Transient,            // Short-lived, discardable
    InterruptSensitive,   // Must not be disrupted during active use
    ContinuityCritical,   // Continuity must survive crash/restart
    BackgroundDominant,   // Rarely focused but always present
    Collaborative         // Shared/synchronized state
};

inline const char* toString(IntentDisposition disposition) {
    switch (disposition) {
        case IntentDisposition::Persistent:         return "Persistent";
        case IntentDisposition::Transient:          return "Transient";
        case IntentDisposition::InterruptSensitive: return "InterruptSensitive";
        case IntentDisposition::ContinuityCritical: return "ContinuityCritical";
        case IntentDisposition::BackgroundDominant: return "BackgroundDominant";
        case IntentDisposition::Collaborative:      return "Collaborative";
    }
    return "Unknown";
}

// How confident is the runtime about this intent assignment?
// Human semantics are probabilistic. Runtime semantics are deterministic.
// This prevents intent inference from masquerading as authoritative truth.
enum class SemanticConfidence {
    Explicit,       // User explicitly set this intent
    Inferred,       // Runtime inferred from behavior patterns
    WeaklyInferred, // Low-confidence inference, may be wrong
    Transient       // Temporarily assigned, expected to change
};

inline const char* toString(SemanticConfidence confidence) {
    switch (confidence) {
        case SemanticConfidence::Explicit:       return "Explicit";
        case SemanticConfidence::Inferred:       return "Inferred";
        case SemanticConfidence::WeaklyInferred: return "WeaklyInferred";
        case SemanticConfidence::Transient:      return "Transient";
    }
    return "Unknown";
}

// Per-workspace intent metadata.
struct WorkspaceIntentState {
    OperationalActivity activity = OperationalActivity::Editing;
    IntentDisposition disposition = IntentDisposition::Persistent;
    SemanticConfidence confidence = SemanticConfidence::Explicit;

    // Human-assigned label (optional, e.g., "Auth Refactor", "Perf Investigation")
    std::string label;

    // When was this intent established? (for adaptive prioritization)
    uint64_t intentEstablishedEpoch = 0;
};

// Phase 7A: Workspace Intent Graph.
//
// Maps WorkspaceId → WorkspaceIntentState.
// ADVISORY ONLY — consumed by session continuity, attention economics,
// and adaptive orchestration. NEVER consumed by runtime kernel for
// correctness decisions.
class WorkspaceIntentGraph {
public:
    WorkspaceIntentGraph() = default;

    void setIntent(WorkspaceId workspaceId,
                   OperationalActivity activity,
                   IntentDisposition disposition,
                   SemanticConfidence confidence = SemanticConfidence::Explicit,
                   const std::string& label = "") {
        WorkspaceIntentState state;
        state.activity = activity;
        state.disposition = disposition;
        state.confidence = confidence;
        state.label = label;
        intents_[workspaceId.value] = state;
    }

    WorkspaceIntentState getIntent(WorkspaceId workspaceId) const {
        auto it = intents_.find(workspaceId.value);
        if (it != intents_.end()) return it->second;
        return {}; // Default: Editing, Persistent, Explicit
    }

    OperationalActivity activityFor(WorkspaceId workspaceId) const {
        return getIntent(workspaceId).activity;
    }

    IntentDisposition dispositionFor(WorkspaceId workspaceId) const {
        return getIntent(workspaceId).disposition;
    }

    SemanticConfidence confidenceFor(WorkspaceId workspaceId) const {
        return getIntent(workspaceId).confidence;
    }

    // Advisory: which workspace should be restored first?
    // ContinuityCritical > Persistent > BackgroundDominant > Transient
    // This is a SUGGESTION, not a command to the runtime.
    bool suggestRestoreBeforeOther(WorkspaceId a, WorkspaceId b) const {
        return dispositionPriority(dispositionFor(a)) >
               dispositionPriority(dispositionFor(b));
    }

    // Advisory: which workspace should degrade first under pressure?
    // Transient > BackgroundDominant > Persistent > ContinuityCritical
    // This is a SUGGESTION, not a command to the runtime.
    bool suggestDegradeBeforeOther(WorkspaceId a, WorkspaceId b) const {
        return dispositionPriority(dispositionFor(a)) <
               dispositionPriority(dispositionFor(b));
    }

    void removeWorkspace(WorkspaceId workspaceId) {
        intents_.erase(workspaceId.value);
    }

    size_t size() const { return intents_.size(); }

private:
    int dispositionPriority(IntentDisposition d) const {
        switch (d) {
            case IntentDisposition::ContinuityCritical: return 100;
            case IntentDisposition::InterruptSensitive: return 80;
            case IntentDisposition::Persistent:         return 60;
            case IntentDisposition::Collaborative:      return 50;
            case IntentDisposition::BackgroundDominant:  return 30;
            case IntentDisposition::Transient:           return 10;
        }
        return 0;
    }

    std::unordered_map<uint64_t, WorkspaceIntentState> intents_;
};

} // namespace morphic
