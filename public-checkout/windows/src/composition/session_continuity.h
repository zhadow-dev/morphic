#pragma once

#include "../core/types.h"
#include "workspace_intent.h"
#include "../experimental/attention_economics.h"
#include <vector>
#include <unordered_map>
#include <string>

namespace morphic {

// Phase 7C: Session Continuity.
//
// Human session continuity — restoring interrupted cognitive momentum,
// not just restoring state.
//
// ARCHITECTURAL INVARIANT:
// Session continuity is an ADVISORY layer. It provides restore suggestions
// and captures human operational context. It NEVER mutates runtime truth.

// Why was the session interrupted?
// This dramatically changes restore ordering, attention prioritization,
// and orchestration hints.
enum class InterruptionReason {
    IntentionalPause,    // User chose to pause/close
    ForcedSuspend,       // OS/system forced suspend (sleep, hibernate)
    CrashRecovery,       // Runtime crashed, recovering
    TemporaryDiversion,  // User switched to urgent task, will return
    UrgentInterruption,  // External interrupt (notification, call)
    SessionEnd           // User ended session (logout, shutdown)
};

inline const char* toString(InterruptionReason reason) {
    switch (reason) {
        case InterruptionReason::IntentionalPause:   return "IntentionalPause";
        case InterruptionReason::ForcedSuspend:       return "ForcedSuspend";
        case InterruptionReason::CrashRecovery:       return "CrashRecovery";
        case InterruptionReason::TemporaryDiversion:  return "TemporaryDiversion";
        case InterruptionReason::UrgentInterruption:  return "UrgentInterruption";
        case InterruptionReason::SessionEnd:          return "SessionEnd";
    }
    return "Unknown";
}

// Context of the interruption — what was happening when the session broke?
struct SessionInterruptionContext {
    InterruptionReason reason = InterruptionReason::IntentionalPause;

    // Which workspace was active at interruption?
    uint64_t activeWorkspaceAtInterruption = 0;

    // Which surface had focus at interruption?
    NodeId focusedSurfaceAtInterruption = kInvalidNodeId;

    // How many workspaces were active?
    int activeWorkspaceCount = 0;

    // Was the runtime under pressure at interruption?
    bool underPressureAtInterruption = false;

    // Semantic confidence in the interruption context
    SemanticConfidence confidence = SemanticConfidence::Explicit;
};

// How should the session be restored?
enum class SessionRestorePolicy {
    RestoreFullContext,    // Restore everything: intent, workflow, attention
    RestoreWorkflowOnly,  // Restore workflow graphs but not fine-grained state
    RestoreTopologyOnly,  // Restore window positions only (legacy mode)
    RestoreMinimal        // Restore only ContinuityCritical workspaces
};

// Per-workspace session state.
struct WorkspaceSessionState {
    uint64_t workspaceKey = 0;
    WorkspaceIntentState intent;
    AttentionLevel lastAttentionLevel = AttentionLevel::Background;
    bool wasFocusedAtInterruption = false;
};

// Complete session state — captures human operational context.
// ADVISORY: consumed by AdaptiveOrchestrator for restore suggestions.
// NEVER consumed by runtime kernel for correctness.
struct SessionState {
    SessionInterruptionContext interruption;
    SessionRestorePolicy restorePolicy = SessionRestorePolicy::RestoreFullContext;

    std::vector<WorkspaceSessionState> workspaces;

    // Advisory: what restore policy makes sense for this interruption?
    static SessionRestorePolicy suggestPolicyForInterruption(InterruptionReason reason) {
        switch (reason) {
            case InterruptionReason::CrashRecovery:
                return SessionRestorePolicy::RestoreMinimal;
            case InterruptionReason::ForcedSuspend:
                return SessionRestorePolicy::RestoreFullContext;
            case InterruptionReason::IntentionalPause:
                return SessionRestorePolicy::RestoreFullContext;
            case InterruptionReason::TemporaryDiversion:
                return SessionRestorePolicy::RestoreWorkflowOnly;
            case InterruptionReason::UrgentInterruption:
                return SessionRestorePolicy::RestoreWorkflowOnly;
            case InterruptionReason::SessionEnd:
                return SessionRestorePolicy::RestoreTopologyOnly;
        }
        return SessionRestorePolicy::RestoreTopologyOnly;
    }
};

} // namespace morphic
