#ifndef RUNNER_TEMPORAL_FRAME_SCHEDULER_H_
#define RUNNER_TEMPORAL_FRAME_SCHEDULER_H_

#include <windows.h>

#include <cstdint>
#include <string>

#include "temporal/critical_path_trace.h"
#include "temporal/epoch_budget.h"
#include "temporal/epoch_reentrancy_guard.h"
#include "temporal/epoch_telemetry_adapter.h"
#include "temporal/frame_epoch.h"

class FrameClock;

// PHASE 8B.7 — FrameScheduler (Epoch Runtime).
//
// The runtime's outer loop in epoch mode. It WRAPS Win32 dispatch inside
// runtime-owned epochs — it does NOT replace Win32. Each epoch:
//
//   Wait          MsgWaitForMultipleObjectsEx on (waitable timer | input | APC)
//   PreDispatch   measure the wait→dispatch latency
//   InputDispatch BOUNDED + YIELDING PeekMessage/Dispatch loop (EpochBudget)
//   RuntimeUpdate FrameClock::PumpEpoch(dt_ms) — drives ALL subscribers
//   Commit        reserved (8B.8); empty today
//   Recovery      grade severity, populate the FrameEpoch, feed telemetry
//
// BOUNDARY LAW: the scheduler owns EPOCH PACING ONLY. It must never become the
// orchestration brain, interaction owner, projection owner, or compositor owner.
// Its sole runtime coupling is a FrameClock* it pumps. Everything else rides the
// preserved FrameClock subscriber contract.
//
// OWNERSHIP NOTE: nested pumps (modal dialogs, shell menus, IME, COM waits) are
// now the scheduler's responsibility to OBSERVE — it cannot prevent them (the OS
// owns those loops) but it detects them via EpochReentrancyGuard so an epoch
// suspended by one is flagged, not silently mispaced.
class FrameScheduler {
 public:
  static constexpr UINT kCadenceIntervalMs = 16;  // ~60 Hz epoch cadence

  // `clock` is pumped each RuntimeUpdate; it must outlive the scheduler (the
  // scheduler is stack-local in wWinMain, the clock lives in MorphicRuntime).
  explicit FrameScheduler(FrameClock* clock);
  ~FrameScheduler();

  FrameScheduler(const FrameScheduler&) = delete;
  FrameScheduler& operator=(const FrameScheduler&) = delete;

  // Runs the epoch loop until WM_QUIT. Returns the WM_QUIT exit code (so wWinMain
  // can return it exactly like the legacy GetMessage loop did).
  int Run();

  // Write the accumulated CriticalPathTrace report (called at shutdown). Path is
  // typically next to the exe. Safe to call even if Run never started.
  bool WriteReport(const std::string& path) const;

  const morphic::CriticalPathTrace& trace() const { return trace_; }

 private:
  // One epoch body. Returns false when WM_QUIT was seen (loop should exit);
  // sets *exit_code when it returns false.
  bool RunEpoch(int* exit_code);

  // Blocks until the timer signals OR input/APC arrives. Returns the classified
  // wake reason and (out) whether a WM_QUIT was observed during the wait's input
  // probe (rare; normally seen in dispatch).
  morphic::EpochWakeReason WaitForWake();

  // Bounded + yielding dispatch. Fills epoch.messages_dispatched and sets
  // epoch.starvation_reason / epoch.budget_pressure if it bails on a full queue.
  // Returns false when WM_QUIT was dispatched; sets *exit_code.
  bool DispatchBounded(morphic::FrameEpoch* epoch, int* exit_code);

  // Grade overrun severity from the epoch's measured signals.
  morphic::EpochOverrunSeverity GradeSeverity(const morphic::FrameEpoch& epoch,
                                              double cadence_ms) const;

  double ElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) const;

  FrameClock* clock_;  // pumped each RuntimeUpdate; not owned

  HANDLE timer_ = nullptr;  // waitable timer (high-res if available)
  bool high_res_timer_ = false;

  LARGE_INTEGER qpc_freq_{};
  LARGE_INTEGER last_epoch_start_{};  // for epoch-to-epoch cadence
  uint64_t next_epoch_id_ = 1;

  EpochBudget budget_;
  EpochReentrancyGuard reentrancy_;
  morphic::CriticalPathTrace trace_;
  EpochTelemetryAdapter adapter_;

  bool running_ = false;
};

#endif  // RUNNER_TEMPORAL_FRAME_SCHEDULER_H_
