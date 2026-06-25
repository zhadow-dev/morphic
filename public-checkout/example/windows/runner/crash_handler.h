#ifndef RUNNER_CRASH_HANDLER_H_
#define RUNNER_CRASH_HANDLER_H_

// FIELD-SAFETY FLOOR — local crash capture (no network, no phone-home).
//
// Installs a process-wide last-resort handler so that if Morphic dies in the
// field (access violation, unhandled C++ throw, std::terminate) it leaves
// evidence behind instead of vanishing:
//
//   <exe dir>\morphic_crash_YYYYMMDD_HHMMSS.dmp   — a minidump (stacks of all
//                                                   threads + module list)
//   morphic_forensic.log                          — a final [CRASH] line with
//                                                   the exception code/address
//
// The dump is written IN-PROCESS from the unhandled-exception filter. That is
// the pragmatic standard and captures the overwhelming majority of crashes; a
// crash that has corrupted the loader/heap badly enough to defeat dbghelp is
// the documented limit (a separate watchdog process would be the next step,
// deferred until lived-in proves it's needed).
//
// This module does NOT transmit anything anywhere. Uploading these artifacts is
// a separate, outward-facing decision (consent / privacy / infra).
namespace crash {

// Install the unhandled-exception filter + terminate handler. Call once, as
// early in wWinMain as possible (right after forensic::Init).
void Install();

}  // namespace crash

#endif  // RUNNER_CRASH_HANDLER_H_
