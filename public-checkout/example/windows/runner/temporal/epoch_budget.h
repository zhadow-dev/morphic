#ifndef RUNNER_TEMPORAL_EPOCH_BUDGET_H_
#define RUNNER_TEMPORAL_EPOCH_BUDGET_H_

// PHASE 8B.7 — EpochBudget.
//
// The single guard that keeps bounded dispatch from silently degenerating into
// hidden queue draining. If the scheduler drained the whole queue every epoch,
// Windows would still implicitly own pacing — the epoch boundary would be a lie.
//
// Three independent caps; the dispatch loop stops at whichever it hits first:
//   - max_messages_per_epoch  : hard ceiling per epoch (defense-in-depth)
//   - max_dispatch_duration_ms : wall-clock cap on the dispatch phase
//   - message_batch_size      : YIELD point — process this many, then break and
//                               end the epoch EVEN IF the queue is still full,
//                               so replay/resize/mouse floods cannot monopolize
//                               the runtime. The remaining messages are handled
//                               next epoch (the next wake is immediate because
//                               MWMO_INPUTAVAILABLE reports the pending input).
//
// Tuning intent (not vsync-locked, deliberately conservative):
//   At ~16ms cadence with an ~8ms dispatch cap, dispatch can never eat the whole
//   frame budget — the RuntimeUpdate phase is guaranteed room. batch_size 16 is
//   well above the steady-state per-epoch message count for a 2-surface runtime
//   (a handful of WM_MOUSEMOVE + paint), so it only engages under genuine floods.
struct EpochBudget {
  int max_messages_per_epoch = 64;
  double max_dispatch_duration_ms = 8.0;
  int message_batch_size = 16;
};

#endif  // RUNNER_TEMPORAL_EPOCH_BUDGET_H_
