#ifndef RUNNER_TEMPORAL_EPOCH_TELEMETRY_ADAPTER_H_
#define RUNNER_TEMPORAL_EPOCH_TELEMETRY_ADAPTER_H_

#include "temporal/critical_path_trace.h"
#include "temporal/frame_epoch.h"

// PHASE 8B.7 — EpochTelemetryAdapter.
//
// The decoupling seam between the scheduler's epoch model and the (tick-centric)
// CriticalPathTrace probe. The scheduler emits FrameEpochs + per-phase durations;
// the adapter maps them onto the probe's recorders. Neither side knows the other's
// internals.
//
// Why this exists (it is NOT ceremony): later there will be MULTIPLE epoch
// streams — runtime epochs, projection epochs, presentation epochs, animation
// epochs (8B.8+). Each can get its OWN CriticalPathTrace instance fed by its OWN
// adapter, without any of them entangling the others' schema. Fusing the epoch
// struct directly into the probe now would make that separation impossible later.
//
// Per-phase durations are passed explicitly (PhaseTimings) rather than living on
// FrameEpoch, because the epoch struct is the durable telemetry record and we
// don't want six float fields on it that only the adapter ever reads.

class EpochTelemetryAdapter {
 public:
  // Phase durations the scheduler measured for this epoch (ms). Any phase that
  // did not run is 0.
  struct PhaseTimings {
    double pre_dispatch_ms = 0.0;
    double input_dispatch_ms = 0.0;
    double runtime_update_ms = 0.0;
  };

  explicit EpochTelemetryAdapter(morphic::CriticalPathTrace* trace)
      : trace_(trace) {}

  // Map one fully-populated epoch (post-Recovery) into the probe.
  void Record(const morphic::FrameEpoch& epoch, const PhaseTimings& timings) {
    if (!trace_) return;

    // Cadence spine + total duration.
    trace_->recordEpochStart(epoch.start_qpc);
    trace_->recordEpochDuration(epoch.duration_ms);

    // Per-phase cost.
    trace_->recordPreDispatchCost(timings.pre_dispatch_ms);
    trace_->recordInputDispatchCost(timings.input_dispatch_ms);
    trace_->recordRuntimeUpdateCost(timings.runtime_update_ms);

    // Volume + categorical dimensions.
    trace_->recordMessagesDispatched(epoch.messages_dispatched);
    trace_->recordWakeReason(epoch.wake_reason);
    trace_->recordStarvation(epoch.starvation_reason);
    trace_->recordBudgetPressure(epoch.budget_pressure);
    trace_->recordSeverity(epoch.severity);
    trace_->recordNestedDepth(epoch.nested_depth);
  }

 private:
  morphic::CriticalPathTrace* trace_;  // not owned; outlives the adapter
};

#endif  // RUNNER_TEMPORAL_EPOCH_TELEMETRY_ADAPTER_H_
