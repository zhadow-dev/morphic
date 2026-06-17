#ifndef RUNNER_TEMPORAL_FRAME_EPOCH_H_
#define RUNNER_TEMPORAL_FRAME_EPOCH_H_

#include <windows.h>

#include <cstdint>

// PHASE 8B.7 — RUNTIME TEMPORAL AUTHORITY.
//
// The temporal vocabulary of the epoch runtime. This header is PURE types — no
// behavior, no Win32 calls beyond LARGE_INTEGER. It defines the spine that the
// FrameScheduler, EpochReentrancyGuard, CriticalPathTrace, and
// EpochTelemetryAdapter all share.
//
// Design note (why every enum is fully populated NOW, not deferred):
//   Telemetry schema calcifies. Once reports / replay traces / histograms start
//   accumulating against an enum, changing it is a data-migration problem, not a
//   code change. So PreDispatch, every wake reason, every budget-pressure source,
//   and every severity grade exist from day one even if some are not yet emitted.
//
// Schema-naming rule (Temporal ABI stability):
//   Names here describe the EPOCH model, never the implementation that happens to
//   drive it today. There is no "wm_timer" or "tick" anywhere — a tick is a
//   FrameClock concept; an epoch is the runtime's own unit of time. If the
//   waitable timer is later replaced by a vsync source, none of these names lie.

namespace morphic {

// The phases a single epoch passes through, in order. Idle is the resting state
// between epochs (blocked in the wait).
//
//   Wake → PreDispatch → InputDispatch → RuntimeUpdate → Commit → (NestedPump) → Recovery
//
// PreDispatch:   time between wake and the first DispatchMessage — exposes
//                pre-dispatch arrival/scheduling latency that would otherwise be
//                invisible (it is neither dispatch cost nor update cost).
// InputDispatch: the bounded + yielding PeekMessage/DispatchMessage loop.
// RuntimeUpdate: FrameClock::PumpEpoch — drives ALL subscribers. Deliberately NOT
//                named "Projection": subscribers include soak, telemetry,
//                integrity audit, and (future) replay, none of which project.
// Commit:        reserved for 8B.8 presentation-commit split; empty today.
// NestedPump:    a secondary message pump ran inside our DispatchMessage (modal
//                dialog, shell menu, IME, COM wait). Detected, never driven.
// Recovery:      post-epoch bookkeeping + severity grading.
enum class EpochPhase {
  Idle,
  PreDispatch,
  InputDispatch,
  RuntimeUpdate,
  Commit,
  NestedPump,
  Recovery,
};

// Why the epoch woke. Classified from the MsgWaitForMultipleObjectsEx return so
// epoch cause attribution stays unambiguous — a cadence slip caused by a flood of
// input wakes is a different problem from one caused by late timer wakes.
enum class EpochWakeReason {
  Unknown,
  TimerWake,    // the waitable timer signaled (cadence wake)
  InputWake,    // WAIT_OBJECT_0 + nCount: queue input arrived before the timer
  ApcWake,      // WAIT_IO_COMPLETION: an alertable APC ran
  SpuriousWake, // returned without a clear cause (defensive bucket)
};

// Why an epoch's work was cut short (or judged starved). None = healthy epoch.
enum class StarvationReason {
  None,
  BudgetExhausted,   // hit max_messages_per_epoch / message_batch_size
  DurationExceeded,  // hit max_dispatch_duration_ms
  NestedPump,        // a nested pump consumed the epoch's time
  ModalLoop,         // an external modal loop owned the thread
};

// WHICH subsystem applied the budget pressure. The point is to never report only
// "something was slow": an input flood starving the update phase is a distinct
// diagnosis from the update phase itself overrunning.
enum class BudgetPressureSource {
  None,
  InputQueue,     // bounded dispatch bailed on a still-full queue
  RuntimeUpdate,  // PumpEpoch overran the remaining budget
  NestedPump,     // a nested pump ate the budget
  ExternalModal,  // an external modal loop ate the budget
};

// Graded health of an epoch, derived in Recovery from cadence slip + starvation
// duration + dispatch debt + nested depth. Enables future runtime health grading
// (extraction, compositor sync, presentation coordination all need a single
// "how bad is right now" signal).
enum class EpochOverrunSeverity {
  Nominal,       // within budget; cadence on target
  Minor,         // slight slip, no functional impact
  Major,         // visible cadence slip
  Critical,      // sustained starvation / large debt
  Catastrophic,  // runaway nested depth / pump owned by something else
};

// One runtime tick of the epoch runtime. The temporal spine: every wake produces
// exactly one FrameEpoch, populated as it passes through its phases and handed to
// the telemetry adapter at the end.
struct FrameEpoch {
  uint64_t id = 0;                 // monotonic, 0 = uninitialized
  LARGE_INTEGER start_qpc{};       // QPC at wake
  EpochPhase phase = EpochPhase::Idle;  // current/last phase reached
  double duration_ms = 0.0;        // wake → end of Recovery

  EpochWakeReason wake_reason = EpochWakeReason::Unknown;
  StarvationReason starvation_reason = StarvationReason::None;
  BudgetPressureSource budget_pressure = BudgetPressureSource::None;
  EpochOverrunSeverity severity = EpochOverrunSeverity::Nominal;

  int nested_depth = 0;            // max reentrancy depth observed this epoch
  int messages_dispatched = 0;     // count actually Dispatched in InputDispatch
};

// --- Stable string forms (for forensic logs + JSON schema) ---
// These strings ARE the telemetry ABI. Do not rename casually.
inline const char* ToString(EpochPhase p) {
  switch (p) {
    case EpochPhase::Idle:          return "idle";
    case EpochPhase::PreDispatch:   return "pre_dispatch";
    case EpochPhase::InputDispatch: return "input_dispatch";
    case EpochPhase::RuntimeUpdate: return "runtime_update";
    case EpochPhase::Commit:        return "commit";
    case EpochPhase::NestedPump:    return "nested_pump";
    case EpochPhase::Recovery:      return "recovery";
  }
  return "?";
}

inline const char* ToString(EpochWakeReason r) {
  switch (r) {
    case EpochWakeReason::Unknown:      return "unknown";
    case EpochWakeReason::TimerWake:    return "timer";
    case EpochWakeReason::InputWake:    return "input";
    case EpochWakeReason::ApcWake:      return "apc";
    case EpochWakeReason::SpuriousWake: return "spurious";
  }
  return "?";
}

inline const char* ToString(StarvationReason r) {
  switch (r) {
    case StarvationReason::None:             return "none";
    case StarvationReason::BudgetExhausted:  return "budget_exhausted";
    case StarvationReason::DurationExceeded: return "duration_exceeded";
    case StarvationReason::NestedPump:       return "nested_pump";
    case StarvationReason::ModalLoop:        return "modal_loop";
  }
  return "?";
}

inline const char* ToString(BudgetPressureSource s) {
  switch (s) {
    case BudgetPressureSource::None:          return "none";
    case BudgetPressureSource::InputQueue:    return "input_queue";
    case BudgetPressureSource::RuntimeUpdate: return "runtime_update";
    case BudgetPressureSource::NestedPump:    return "nested_pump";
    case BudgetPressureSource::ExternalModal: return "external_modal";
  }
  return "?";
}

inline const char* ToString(EpochOverrunSeverity s) {
  switch (s) {
    case EpochOverrunSeverity::Nominal:      return "nominal";
    case EpochOverrunSeverity::Minor:        return "minor";
    case EpochOverrunSeverity::Major:        return "major";
    case EpochOverrunSeverity::Critical:     return "critical";
    case EpochOverrunSeverity::Catastrophic: return "catastrophic";
  }
  return "?";
}

}  // namespace morphic

#endif  // RUNNER_TEMPORAL_FRAME_EPOCH_H_
