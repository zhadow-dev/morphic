#ifndef RUNNER_FORENSIC_TRACE_H_
#define RUNNER_FORENSIC_TRACE_H_

#include <windows.h>

#include <string>

// PHASE 1A forensic instrumentation. Writes a timestamped, thread-tagged trace
// of native runtime startup to <exe dir>\morphic_forensic.log.
//
// This is PURE instrumentation: it observes and records, and has no effect on
// runtime behavior. Line format:
//
//   [+ss.mmm][T:tid][SUBSYS] message  @wall=HH:MM:SS.mmm
namespace forensic {

// Capture the time base and truncate the log file. Safe to call once at startup.
void Init();

// Append one event line.
void Log(const char* subsystem, const std::string& message);

// Crash-time append. Safe to call from an unhandled-exception / terminate
// handler: fixed buffers only, no std::string, and never blocks on the trace
// mutex (a possibly-interleaved line beats a deadlock when a faulting thread
// holds the lock). Flushed to disk immediately.
void LogCrash(const char* subsystem, const char* message);

// True the FIRST time a given window message id is seen; false afterwards.
// Used to log high-frequency messages exactly once.
bool FirstSeen(UINT message);

// Readable name for a Win32 message id (curated set; falls back to hex).
const char* MessageName(UINT message);

// Decode GWL_STYLE / GWL_EXSTYLE into named bits and log them.
void DumpWindowStyles(const char* subsystem, HWND hwnd, const char* tag);

// Read back DWM state where the API permits (NC rendering, cloak, frame bounds).
void DumpDwmState(HWND hwnd);

// Dump the live HWND hierarchy rooted at `root` (class, styles, parent, owner).
void DumpHwndGraph(HWND root);

}  // namespace forensic

#endif  // RUNNER_FORENSIC_TRACE_H_
