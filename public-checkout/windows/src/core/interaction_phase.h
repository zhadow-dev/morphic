#pragma once

namespace morphic {

// Phase 8B.6 — Interaction Phase Attribution.
//
// Every timing sample must carry an InteractionPhase.
// Without phase attribution:
//   - averages become meaningless
//   - spikes become unactionable
//   - p99 analysis becomes noise
//
// This is the difference between:
//   "avg tick = 27ms"  (useless)
// and:
//   "avg tick during drag = 18ms, during grouped-drag = 42ms"  (actionable)
//
// RULE: This enum is OBSERVATIONAL. It does NOT drive orchestration.
//       It exists solely for diagnostic attribution.
enum class InteractionPhase {
    Idle,           // No active interaction
    Drag,           // Single-surface drag
    Resize,         // Single-surface resize
    GroupedDrag,    // Multi-surface grouped drag
    CaptureLoss,    // External capture steal in progress
    Destroy,        // Surface destruction in progress
    Reconcile,      // Commit scheduler reconciliation
    Projection,     // SetWindowPos / DeferWindowPos flush
    Audit,          // Invariant/temporal/ordering validation
    Replay,         // Deterministic replay playback
    Stress,         // Stress harness / soak test
    Activation,     // Focus/activation transition
    Governance,     // Workload controller / governance drain
};

inline const char* toString(InteractionPhase phase) {
    switch (phase) {
        case InteractionPhase::Idle:         return "idle";
        case InteractionPhase::Drag:         return "drag";
        case InteractionPhase::Resize:       return "resize";
        case InteractionPhase::GroupedDrag:   return "grouped_drag";
        case InteractionPhase::CaptureLoss:  return "capture_loss";
        case InteractionPhase::Destroy:      return "destroy";
        case InteractionPhase::Reconcile:    return "reconcile";
        case InteractionPhase::Projection:   return "projection";
        case InteractionPhase::Audit:        return "audit";
        case InteractionPhase::Replay:       return "replay";
        case InteractionPhase::Stress:       return "stress";
        case InteractionPhase::Activation:   return "activation";
        case InteractionPhase::Governance:   return "governance";
    }
    return "unknown";
}

}  // namespace morphic
