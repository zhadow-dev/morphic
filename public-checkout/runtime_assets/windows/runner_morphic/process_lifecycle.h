#ifndef RUNNER_PROCESS_LIFECYCLE_H_
#define RUNNER_PROCESS_LIFECYCLE_H_

#include <atomic>

namespace morphic {

// ENGINE RETENTION — PHASE R0: exit-reset elimination.
//
// controller_.reset() (FlutterViewController destruction) hits the embedder's
// teardown race (flutter_windows.dll+0xCEEE25, ~4% per call, full-process
// crash). At APP EXIT that reset buys NOTHING: process termination already
// reclaims the engine's memory, threads, handles, GPU/COM resources. So once
// graceful shutdown begins we LEAK engines (unique_ptr::release, no destructor)
// instead of resetting them — trading a self-inflicted crash class for the OS
// reclaim that was going to happen anyway.
//
// Set true at the single graceful-exit trigger (shell_root WM_CLOSE) BEFORE the
// owner-chain DestroyWindow cascade, so every render_source WM_DESTROY in the
// cascade sees it; and defensively after the message loop for any other path.
// See doc/ENGINE_RETENTION_POOL_DESIGN.md (Phase R0).
//
// NOTE: this changes ONLY exit behavior. During-session single-surface teardown
// (SurfaceManager::TickTeardown / ReleaseEngine) still resets normally — that is
// Phase R1 (retention), a separate change.
inline std::atomic<bool>& ProcessExiting() {
  static std::atomic<bool> v{false};
  return v;
}

// R0 — ABRUPT EXIT. Measurement showed exit doesn't only crash on engine resets:
// destroying the shell_root cascades into the spatial hosts (WGC capture worker
// threads, DComp, swapchains) and THAT teardown crashes too, before the engines
// even finish leaking. The whole graceful exit cascade is pointless — process
// termination reclaims windows, engines, GPU/COM, threads regardless. So at
// graceful close we SKIP the cascade (don't DestroyWindow the shell_root) and
// terminate the process directly, running NO teardown at all. Forensic logging
// flushes+closes per line, so nothing buffered is lost. Reversible: false
// restores the old destroy-cascade + destructor path.
inline constexpr bool kR0AbruptExit = true;

}  // namespace morphic

#endif  // RUNNER_PROCESS_LIFECYCLE_H_
