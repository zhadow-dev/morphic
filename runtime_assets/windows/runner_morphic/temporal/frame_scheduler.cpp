#include "temporal/frame_scheduler.h"

#include "forensic_trace.h"
#include "frame_clock.h"

// PHASE 8B.7 — FrameScheduler implementation.
//
// CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (0x00000002) lands a ~0.5ms-resolution
// timer on Windows 10 1803+. Older SDKs may not define it; define defensively.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

using morphic::BudgetPressureSource;
using morphic::EpochOverrunSeverity;
using morphic::EpochPhase;
using morphic::EpochWakeReason;
using morphic::FrameEpoch;
using morphic::StarvationReason;

FrameScheduler::FrameScheduler(FrameClock* clock)
    : clock_(clock), adapter_(&trace_) {
  QueryPerformanceFrequency(&qpc_freq_);
  last_epoch_start_.QuadPart = 0;

  // Prefer the high-resolution waitable timer; fall back to the legacy one.
  timer_ = CreateWaitableTimerExW(
      nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
      TIMER_ALL_ACCESS);
  if (timer_ != nullptr) {
    high_res_timer_ = true;
  } else {
    timer_ = CreateWaitableTimerW(nullptr, FALSE /*auto-reset*/, nullptr);
    high_res_timer_ = false;
  }

  if (timer_ == nullptr) {
    forensic::Log("SCHEDULER",
                  "FATAL: CreateWaitableTimer failed err=" +
                      std::to_string(GetLastError()) +
                      " — epoch loop will fall back to input-only waits");
  } else {
    // Periodic timer. Due time is a negative relative 100ns interval; period is
    // in ms. We re-arm explicitly each epoch too (belt-and-suspenders against
    // drift), but a periodic timer keeps cadence if an epoch overruns.
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(kCadenceIntervalMs) * 10000LL;  // ms→100ns
    if (!SetWaitableTimer(timer_, &due, static_cast<LONG>(kCadenceIntervalMs),
                          nullptr, nullptr, FALSE)) {
      forensic::Log("SCHEDULER", "SetWaitableTimer failed err=" +
                                     std::to_string(GetLastError()));
    }
    forensic::Log("SCHEDULER",
                  std::string("waitable timer armed (") +
                      (high_res_timer_ ? "high-res" : "legacy") +
                      ", interval=" + std::to_string(kCadenceIntervalMs) + "ms)");
  }
}

FrameScheduler::~FrameScheduler() {
  if (timer_) {
    CancelWaitableTimer(timer_);
    CloseHandle(timer_);
    timer_ = nullptr;
  }
}

double FrameScheduler::ElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) const {
  if (qpc_freq_.QuadPart == 0) return 0.0;
  return (end.QuadPart - start.QuadPart) * 1000.0 /
         static_cast<double>(qpc_freq_.QuadPart);
}

int FrameScheduler::Run() {
  running_ = true;
  forensic::Log("SCHEDULER", "Run: entering epoch loop");
  int exit_code = 0;
  while (RunEpoch(&exit_code)) {
    // loop until WM_QUIT
  }
  running_ = false;
  forensic::Log("SCHEDULER",
                "Run: epoch loop exited code=" + std::to_string(exit_code) +
                    " epochs=" + std::to_string(trace_.epochCount()));
  return exit_code;
}

bool FrameScheduler::RunEpoch(int* exit_code) {
  // Reentrancy scope: if a nested pump re-enters the thread's loop while this
  // epoch's DispatchMessage runs, the depth observed here climbs. depth==1 is the
  // normal, non-nested case.
  EpochReentrancyGuard::Scope reentry(reentrancy_);

  FrameEpoch epoch;
  epoch.id = next_epoch_id_++;
  epoch.nested_depth = reentry.depth();

  EpochTelemetryAdapter::PhaseTimings timings{};

  // ---- Wait ----
  epoch.phase = EpochPhase::Idle;
  epoch.wake_reason = WaitForWake();

  // Epoch officially begins at wake.
  QueryPerformanceCounter(&epoch.start_qpc);

  // Cadence = wall gap between this epoch's wake and the previous one.
  double cadence_ms = 0.0;
  if (last_epoch_start_.QuadPart != 0) {
    cadence_ms = ElapsedMs(last_epoch_start_, epoch.start_qpc);
  }
  last_epoch_start_ = epoch.start_qpc;

  // ---- PreDispatch ----
  // The interval between wake and the first DispatchMessage. With dispatch
  // immediately following, this captures only the scheduling/bookkeeping gap;
  // it becomes meaningful once pre-dispatch work (input coalescing, etc.) lands.
  epoch.phase = EpochPhase::PreDispatch;
  LARGE_INTEGER pre_start{};
  QueryPerformanceCounter(&pre_start);
  // (no pre-dispatch work yet) — measure the boundary so the metric exists.
  LARGE_INTEGER pre_end{};
  QueryPerformanceCounter(&pre_end);
  timings.pre_dispatch_ms = ElapsedMs(pre_start, pre_end);

  // ---- InputDispatch ----
  epoch.phase = EpochPhase::InputDispatch;
  LARGE_INTEGER disp_start{};
  QueryPerformanceCounter(&disp_start);
  if (!DispatchBounded(&epoch, exit_code)) {
    // WM_QUIT dispatched. Still record this (partial) epoch so the trace shows
    // the shutdown frame, then signal the loop to stop.
    LARGE_INTEGER disp_end{};
    QueryPerformanceCounter(&disp_end);
    timings.input_dispatch_ms = ElapsedMs(disp_start, disp_end);
    epoch.phase = EpochPhase::Recovery;
    epoch.duration_ms = ElapsedMs(epoch.start_qpc, disp_end);
    epoch.severity = GradeSeverity(epoch, cadence_ms);
    adapter_.Record(epoch, timings);
    return false;
  }
  LARGE_INTEGER disp_end{};
  QueryPerformanceCounter(&disp_end);
  timings.input_dispatch_ms = ElapsedMs(disp_start, disp_end);

  // ---- RuntimeUpdate ----
  // Drives ALL FrameClock subscribers (interaction sessions, soak, telemetry,
  // integrity audit, ...). dt is the epoch-to-epoch cadence; on the first epoch
  // cadence_ms == 0 which subscribers already handle (FrameClock's first tick is
  // also dt==0).
  epoch.phase = EpochPhase::RuntimeUpdate;
  LARGE_INTEGER upd_start{};
  QueryPerformanceCounter(&upd_start);
  if (clock_) {
    clock_->PumpEpoch(cadence_ms);
  }
  LARGE_INTEGER upd_end{};
  QueryPerformanceCounter(&upd_end);
  timings.runtime_update_ms = ElapsedMs(upd_start, upd_end);

  // If the update phase blew the remaining budget, attribute it (only when no
  // stronger pressure source was already set by dispatch).
  if (timings.runtime_update_ms > budget_.max_dispatch_duration_ms &&
      epoch.budget_pressure == BudgetPressureSource::None) {
    epoch.budget_pressure = BudgetPressureSource::RuntimeUpdate;
    if (epoch.starvation_reason == StarvationReason::None) {
      epoch.starvation_reason = StarvationReason::DurationExceeded;
    }
  }

  // ---- Commit ---- (reserved for 8B.8 presentation-commit split; empty today)
  epoch.phase = EpochPhase::Commit;

  // ---- NestedPump attribution ----
  if (epoch.nested_depth > 1) {
    epoch.phase = EpochPhase::NestedPump;
    epoch.starvation_reason = StarvationReason::NestedPump;
    if (epoch.budget_pressure == BudgetPressureSource::None) {
      epoch.budget_pressure = BudgetPressureSource::NestedPump;
    }
  }

  // ---- Recovery ----
  epoch.phase = EpochPhase::Recovery;
  LARGE_INTEGER epoch_end{};
  QueryPerformanceCounter(&epoch_end);
  epoch.duration_ms = ElapsedMs(epoch.start_qpc, epoch_end);
  epoch.severity = GradeSeverity(epoch, cadence_ms);

  // Loud trace only for genuinely degraded epochs — steady-state epochs stay
  // silent so the forensic log isn't flooded at 60 Hz. Telemetry captures all.
  if (epoch.severity >= EpochOverrunSeverity::Major ||
      epoch.starvation_reason != StarvationReason::None ||
      epoch.nested_depth > 1) {
    forensic::Log(
        "SCHEDULER",
        "epoch id=" + std::to_string(epoch.id) +
            " wake=" + morphic::ToString(epoch.wake_reason) +
            " cadence=" + std::to_string(cadence_ms) + "ms" +
            " msgs=" + std::to_string(epoch.messages_dispatched) +
            " depth=" + std::to_string(epoch.nested_depth) +
            " starv=" + morphic::ToString(epoch.starvation_reason) +
            " pressure=" + morphic::ToString(epoch.budget_pressure) +
            " sev=" + morphic::ToString(epoch.severity));
  }

  adapter_.Record(epoch, timings);
  return true;
}

morphic::EpochWakeReason FrameScheduler::WaitForWake() {
  if (timer_ == nullptr) {
    // No timer — degrade to an input-only wait so the loop still functions.
    MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT,
                                MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);
    return EpochWakeReason::InputWake;
  }

  const HANDLE handles[1] = {timer_};
  const DWORD r = MsgWaitForMultipleObjectsEx(
      1, handles, INFINITE, QS_ALLINPUT,
      MWMO_INPUTAVAILABLE | MWMO_ALERTABLE);

  if (r == WAIT_OBJECT_0) {
    return EpochWakeReason::TimerWake;       // the waitable timer signaled
  }
  if (r == WAIT_OBJECT_0 + 1) {
    return EpochWakeReason::InputWake;        // queue input is available
  }
  if (r == WAIT_IO_COMPLETION) {
    return EpochWakeReason::ApcWake;          // an alertable APC ran
  }
  return EpochWakeReason::SpuriousWake;       // WAIT_FAILED / unexpected
}

bool FrameScheduler::DispatchBounded(FrameEpoch* epoch, int* exit_code) {
  LARGE_INTEGER start{};
  QueryPerformanceCounter(&start);

  int dispatched = 0;
  MSG msg;
  while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      *exit_code = static_cast<int>(msg.wParam);
      epoch->messages_dispatched = dispatched;
      return false;  // tell the loop to exit
    }

    TranslateMessage(&msg);
    DispatchMessage(&msg);
    ++dispatched;

    // --- Budget enforcement (whichever cap hits first) ---

    // Hard ceiling.
    if (dispatched >= budget_.max_messages_per_epoch) {
      epoch->starvation_reason = StarvationReason::BudgetExhausted;
      epoch->budget_pressure = BudgetPressureSource::InputQueue;
      break;
    }

    // Batch yield: process a batch, then break to end the epoch EVEN IF the
    // queue is still full, so floods can't monopolize the runtime. The next
    // wake is immediate (MWMO_INPUTAVAILABLE reports the pending input), so no
    // input is lost — it's just paced across epochs.
    if (dispatched >= budget_.message_batch_size) {
      MSG peek;
      if (PeekMessage(&peek, nullptr, 0, 0, PM_NOREMOVE)) {
        epoch->starvation_reason = StarvationReason::BudgetExhausted;
        epoch->budget_pressure = BudgetPressureSource::InputQueue;
      }
      break;
    }

    // Wall-clock cap.
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (ElapsedMs(start, now) >= budget_.max_dispatch_duration_ms) {
      epoch->starvation_reason = StarvationReason::DurationExceeded;
      epoch->budget_pressure = BudgetPressureSource::InputQueue;
      break;
    }
  }

  epoch->messages_dispatched = dispatched;
  return true;
}

morphic::EpochOverrunSeverity FrameScheduler::GradeSeverity(
    const FrameEpoch& epoch, double cadence_ms) const {
  // Cascading nested pumps are the worst case regardless of timing.
  if (epoch.nested_depth >= 4) return EpochOverrunSeverity::Catastrophic;
  if (epoch.nested_depth == 3) return EpochOverrunSeverity::Critical;

  // Cadence slip relative to target. First epoch (cadence==0) is Nominal.
  if (cadence_ms <= 0.0) return EpochOverrunSeverity::Nominal;

  const double target = static_cast<double>(kCadenceIntervalMs);
  if (cadence_ms >= target * 4.0) return EpochOverrunSeverity::Critical;   // >=64ms
  if (cadence_ms >= target * 2.5) return EpochOverrunSeverity::Major;      // >=40ms
  if (cadence_ms >= target * 1.5) return EpochOverrunSeverity::Minor;      // >=24ms

  // On-cadence but starved (budget bailed on a full queue) → at least Minor.
  if (epoch.starvation_reason != StarvationReason::None) {
    return EpochOverrunSeverity::Minor;
  }
  return EpochOverrunSeverity::Nominal;
}

bool FrameScheduler::WriteReport(const std::string& path) const {
  return trace_.writeReport(path);
}
