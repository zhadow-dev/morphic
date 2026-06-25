#ifndef MORPHIC_SRC_INTERACTION_FOCUS_TYPES_H_
#define MORPHIC_SRC_INTERACTION_FOCUS_TYPES_H_

#include "../core/types.h"
#include <cstdint>

namespace morphic {

// -----------------------------------------------------------------------------
// 1. Semantic Navigation Chains (FocusDomain)
// 
// Represents the structural chain a surface belongs to for navigation purposes.
// This is independent of its rendering topology.
// -----------------------------------------------------------------------------
enum class FocusDomain {
    Workspace,            // Part of the main window's tab/surface cycle
    Detached,             // Part of the independent floating window cycle
    Floating,             // Palette/tooling that floats above workspace
    Overlay,              // Notification/transient UI
    Modal                 // Capture layer
};

// -----------------------------------------------------------------------------
// 2. Attention Behavior
// 
// Dictates how a surface participates in focus cycles and whether it traps
// or yields attention.
// -----------------------------------------------------------------------------
enum class AttentionBehavior {
    Interactive,          // Normal surface, participates in focus cycles
    PassiveOverlay,       // Can be visible, but yields focus to underlying surfaces
    ModalCapture,         // Traps focus, suppresses underlying domains
    NonFocusable,         // E.g., background decorators, click-through layers
    WorkspaceBound,       // Interactive but bound strictly to Workspace navigation
    DetachedIndependent   // Independent Alt+Tab / activation identity
};

// -----------------------------------------------------------------------------
// 3. Focus Eligibility
// 
// The dynamic, runtime state of a surface determining if it can legally
// hold semantic focus at this exact moment.
// -----------------------------------------------------------------------------
enum class FocusEligibility {
    Eligible,
    SuppressedByModal,    // A modal is active above this surface
    Suspended,            // The renderer is hibernating/backgrounded
    Hidden,               // The surface is not visible
    DetachedInactive,     // Detached surface but the OS group is inactive
    RuntimeBlocked        // E.g., during initialization/recovery
};

// -----------------------------------------------------------------------------
// 4. Focus Initiation
// 
// Traceability for why a semantic transition was requested.
// -----------------------------------------------------------------------------
enum class FocusInitiator {
    UserInput,            // Mouse click or explicit keyboard shortcut
    RuntimePolicy,        // E.g., auto-focusing on creation
    ModalCapture,         // Modal forcibly stole focus
    WorkspaceSwitch,      // Changing the active workspace/desktop
    SystemActivation,     // OS triggered activation (Alt+Tab, Taskbar)
    RecoveryRestore       // Post-crash or suspend restore
};

// -----------------------------------------------------------------------------
// 5. Restoration Policy
// 
// Determines what happens when the current focused surface is destroyed
// or loses eligibility (e.g., modal closes).
// -----------------------------------------------------------------------------
enum class FocusRestorePolicy {
    PreviousSemanticFocus, // Revert to exactly what was focused before
    PreviousVisibleFocus,  // Revert to the last visible surface in the chain
    WorkspaceDefault,      // Fallback to the primary workspace surface
    DetachedPriority,      // Prefer the most recent detached surface
    None                   // Leave focus adrift
};

// -----------------------------------------------------------------------------
// 6. Global Interaction Intent
// 
// Encoded physical inputs (e.g., Ctrl+Tab) translated into semantic actions.
// The InputRouter produces these, the FocusGraph consumes them.
// -----------------------------------------------------------------------------
enum class InteractionIntent {
    CycleForward,         // e.g. Ctrl+Tab
    CycleBackward,        // e.g. Ctrl+Shift+Tab
    ActivateDetached,     // Bring a detached group to front
    DismissOverlay,       // e.g. Esc on a popup
    ExitModal,            // e.g. Esc on a dialog
    ToggleOverview,       // Enter spatial overview mode
    FocusWorkspace        // Explicitly target the workspace root
};

// -----------------------------------------------------------------------------
// 7. Activation Result
// 
// Feedback from the realization layer (Win32) back to the semantic layer.
// Semantic Truth != Realized Truth.
// -----------------------------------------------------------------------------
enum class ActivationResult {
    Succeeded,            // Win32 granted foreground
    Deferred,             // Foreground lock timeout or pending transition
    DeniedByOS,           // OS actively blocked SetForegroundWindow
    BlockedByModal,       // Morphic blocked due to local modal state
    ExternallyStolen      // Another process grabbed it during transition
};

// -----------------------------------------------------------------------------
// 8. Focus Divergence & Continuity State
// 
// Represents the delta between semantic truth and Win32 realization truth,
// and the overarching state of runtime continuity.
// -----------------------------------------------------------------------------
enum class FocusDivergence {
    None,
    OSDeniedActivation,       // Windows refused SetForegroundWindow
    ExternalFocusSteal,       // Another app stole focus during transition
    ModalConflict,            // OS-level modal blocked us
    DestroyedDuringTransition // Surface died before activation completed
};

enum class DivergenceResolution {
    Retry,
    Ignore,
    FallbackRestore,
    SwitchDomain,
    SuppressUntilExternalFocusReturns
};

// A unique identifier for a discrete transition event to prevent recursion
using EpochId = uint64_t;

struct DivergenceContext {
    int retryCount = 0;
    EpochId originatingEpoch = 0;
    DivergenceResolution previousResolution = DivergenceResolution::Ignore;
};

// Phase 4 Step 4: Continuity State.
// Models the structural integrity of the runtime across suspension and mutation.
enum class ContinuityState {
    Coherent,         // Semantic state matches expected realization
    Fractured,        // E.g. Suspended, or OS-level disruption occurred
    Reconstructing,   // Currently rebuilding state from checkpoints
    Diverged          // Hard failure to reconcile OS with semantic truth
};

// Phase 5A: Hierarchical Continuity.
// Continuity fractures happen at surface level, not just globally.
struct SurfaceContinuityState {
    ContinuityState state = ContinuityState::Coherent;
    EpochId lastCoherentEpoch = 0;
};

// -----------------------------------------------------------------------------
// 9. Core Structures
// -----------------------------------------------------------------------------

struct FocusNode {
    NodeId id;
    FocusDomain domain;
    AttentionBehavior behavior;
    FocusEligibility currentEligibility;
    FocusRestorePolicy restorePolicy;
    SurfaceContinuityState continuity;  // Phase 5A: per-node continuity
};


struct FocusTransition {
    NodeId from;
    NodeId to;
    FocusInitiator initiator;
    InteractionIntent intent; // Optional, if driven by input
    EpochId epoch;
};

// -----------------------------------------------------------------------------
// 10. Focus Decision (Semantic Arbitration Output)
// 
// This is "what SHOULD happen" before realization.
// It is the pure output of the FocusArbitrationPolicy.
// -----------------------------------------------------------------------------
struct FocusDecision {
    NodeId target;
    FocusEligibility eligibility;
    AttentionBehavior behavior;
    FocusInitiator initiator;
    ActivationResult expectedActivation;
    bool requiresModalDismissal = false;
    bool requiresRestore = false;
};

// -----------------------------------------------------------------------------
// 11. Attention Checkpoint (Live Semantic Anchor)
// 
// Captures the semantic state at a specific point in time to allow
// deterministic restoration after suspend/modal/crash.
// WARNING: This contains live runtime assumptions (epochs, node IDs)
// and MUST NEVER BE SERIALIZED.
// -----------------------------------------------------------------------------
struct AttentionCheckpoint {
    NodeId semanticFocus = kInvalidNodeId;
    FocusDomain domain = FocusDomain::Workspace;
    FocusRestorePolicy restorePolicy = FocusRestorePolicy::PreviousSemanticFocus;
    EpochId epoch = 0;
    bool valid = false;
};

// -----------------------------------------------------------------------------
// 12. Modal Suppression Semantics
// 
// Represents the policy of an active modal capture.
// -----------------------------------------------------------------------------
struct ModalSuppressionPolicy {
    NodeId modalNodeId;
    bool blocksWorkspace;      // Does it suppress the main workspace?
    bool blocksDetached;       // Does it suppress detached windows?
    bool allowClickThrough;    // Can underlying windows receive passive clicks?
};

} // namespace morphic

#endif // MORPHIC_SRC_INTERACTION_FOCUS_TYPES_H_
