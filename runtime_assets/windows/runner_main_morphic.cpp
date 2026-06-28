#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <windows.h>
#include <mmsystem.h>  // timeBeginPeriod / timeEndPeriod (winmm.lib)

#include "crash_handler.h"
#include "forensic_trace.h"
#include "frame_clock.h"
#include "morphic_runtime.h"
#include "process_lifecycle.h"
#include "temporal/frame_scheduler.h"
#include "utils.h"

namespace {
// PHASE 8B.7 — DUAL RUNTIME PATH (compile-time, per spec — pump ownership is
// foundational state; live-switching temporal substrates is architectural
// poison). false = legacy WM_TIMER + GetMessage loop (unchanged from 7C/7E).
// true  = epoch-governed FrameScheduler (waitable timer + bounded dispatch).
//
// Land at false so the merge is inert; flip + rebuild for the temporal-gate soak.
// Either way the boot trace logs `[FRAME] runtime=...` so any accumulated
// telemetry is unambiguously attributable to the substrate that produced it.
constexpr bool kEpochRuntime = false;
}  // namespace

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  forensic::Init();
  forensic::Log("BOOT", "wWinMain entered (process start baseline)");

  // FIELD SAFETY: install last-resort crash capture before anything else can
  // fault. Leaves a minidump + [CRASH] log line next to the exe; transmits
  // nothing (local-only).
  crash::Install();

  // PHASE 7E rev4 — bump Windows timer resolution to 1ms for the runtime's
  // lifetime. SetTimer (used by FrameClock and the per-surface message pump)
  // defaults to ~15.6ms granularity, so a 16ms request often lands at 25-32ms
  // under input pressure. timeBeginPeriod(1) brings the clock interrupt down
  // to 1ms, letting timer messages actually meet their requested cadence.
  // Process-wide effect (entire process; slight battery cost on laptops); for
  // an interactive desktop runtime that's acceptable. Balanced with the
  // matching timeEndPeriod(1) at exit.
  timeBeginPeriod(1);
  forensic::Log("BOOT", "timeBeginPeriod(1) — 1ms timer resolution");

  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  forensic::Log("BOOT", "CoInitializeEx done (APARTMENTTHREADED)");

  flutter::DartProject project(L"data");

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));
  forensic::Log("BOOT", "DartProject(\"data\") created; entrypoint args set");

  MorphicRuntime runtime(project);
  forensic::Log("BOOT", "MorphicRuntime constructed; calling Create()");
  if (!runtime.Create()) {
    forensic::Log("BOOT", "MorphicRuntime::Create FAILED -> EXIT_FAILURE");
    return EXIT_FAILURE;
  }
  // PHASE 8B.7 — runtime identity logging. Telemetry is meaningless without
  // knowing which temporal substrate produced it.
  forensic::Log("FRAME", kEpochRuntime ? "runtime=epoch" : "runtime=legacy");

  int exit_code = EXIT_SUCCESS;
  if (kEpochRuntime) {
    // EPOCH PATH. The scheduler owns the outer loop and pumps the runtime's
    // FrameClock once per epoch. SetExternallyDriven(true) BEFORE any subscriber
    // arrives so the clock never installs its own WM_TIMER. The scheduler is
    // scoped so it destructs (CancelWaitableTimer/CloseHandle) BEFORE the runtime
    // is torn down at end of wWinMain — clock outlives scheduler.
    forensic::Log("BOOT", "Create() returned true; entering FrameScheduler loop");
    if (runtime.clock()) {
      runtime.clock()->SetExternallyDriven(true);
    }
    {
      FrameScheduler scheduler(runtime.clock());
      exit_code = scheduler.Run();
      // Write the epoch telemetry next to the exe before the scheduler dies.
      scheduler.WriteReport("morphic_critical_path.json");
    }
    forensic::Log("BOOT", "FrameScheduler loop exited; CoUninitialize");
  } else {
    // LEGACY PATH. Plain GetMessage loop; FrameClock owns its own WM_TIMER.
    forensic::Log("BOOT", "Create() returned true; entering GetMessage loop");
    ::MSG msg;
    while (::GetMessage(&msg, nullptr, 0, 0)) {
      ::TranslateMessage(&msg);
      ::DispatchMessage(&msg);
      // RUNTIME CORE (H0a) — pump the headless bootstrap engine's platform-thread
      // tasks after each message. The engine self-posts wake messages, so
      // GetMessage returns and we drain its task queue here. Inert (no engine)
      // until Create() starts the probe.
      if (auto *be = runtime.bootstrap_engine()) be->ProcessMessages();
    }
    forensic::Log("BOOT", "GetMessage loop exited; CoUninitialize");
  }

  // R0 — the message loop has exited: we are unwinding to process exit.
  morphic::ProcessExiting().store(true);
  if (morphic::kR0AbruptExit) {
    // Terminate WITHOUT running the MorphicRuntime/compositor/engine destructors
    // (graceful teardown only pays the flutter_windows.dll + WGC/DComp teardown
    // crashes for shutdown purity; the OS reclaims everything). Forensic logging
    // flushes per line, so nothing buffered is lost. timeEndPeriod is per-process
    // and reset by the OS on exit. ExitProcess terminates other threads first,
    // so the teardown-race join never runs.
    forensic::Log("BOOT", "R0 abrupt exit - ExitProcess (no destructors)");
    timeEndPeriod(1);
    ::ExitProcess(static_cast<UINT>(exit_code));
  }

  ::CoUninitialize();
  timeEndPeriod(1);  // restore default timer resolution
  forensic::Log("BOOT", "timeEndPeriod(1)");
  return exit_code;
}
