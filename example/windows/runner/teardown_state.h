#ifndef RUNNER_TEARDOWN_STATE_H_
#define RUNNER_TEARDOWN_STATE_H_

namespace morphic {

// PHASE 11 — explicit DEFERRED-TEARDOWN lifecycle (NOT ad-hoc delayed cleanup).
// Visual lifetime and engine lifetime are decoupled: the composited host is gone
// the instant a surface closes (DETACHING/VISUALLY_REMOVED), while the Flutter
// engine drains through QUIESCING -> REAP_PENDING -> DESTROYING, serialized +
// paced by the reaper so flutter_windows.dll's teardown race is suppressed.
// Every transition is logged with a timestamp; a watchdog screams (and, in
// strict mode, fails) if a state overstays its deadline — no zombie engines, no
// "eventually maybe cleanup".
enum class TeardownState {
  kActive = 0,        // live surface (not in the teardown queue)
  kDetaching,         // close requested; semantic/visual detach in flight
  kVisuallyRemoved,   // host dropped; engine still alive (cloaked, no host)
  kQuiescing,         // waiting for the engine to settle before destruction
  kReapPending,       // queued; waiting for the (serialized) destroy slot
  kDestroying,        // engine released + HWND being destroyed (ONE at a time)
  kDestroyed,         // terminal
};

inline const char* TeardownStateName(TeardownState s) {
  switch (s) {
    case TeardownState::kActive: return "ACTIVE";
    case TeardownState::kDetaching: return "DETACHING";
    case TeardownState::kVisuallyRemoved: return "VISUALLY_REMOVED";
    case TeardownState::kQuiescing: return "QUIESCING";
    case TeardownState::kReapPending: return "REAP_PENDING";
    case TeardownState::kDestroying: return "DESTROYING";
    case TeardownState::kDestroyed: return "DESTROYED";
  }
  return "?";
}

}  // namespace morphic

#endif  // RUNNER_TEARDOWN_STATE_H_
