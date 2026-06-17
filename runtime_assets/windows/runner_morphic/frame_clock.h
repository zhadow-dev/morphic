#ifndef RUNNER_FRAME_CLOCK_H_
#define RUNNER_FRAME_CLOCK_H_

#include <windows.h>

#include <functional>
#include <unordered_map>

// PHASE 7C — RUNTIME TEMPORAL AUTHORITY.
//
// FrameClock is the runtime's SINGLE source of frame cadence. Phase 7B drove
// projection from a per-interaction SetTimer on the captured surface's HWND;
// that worked for one interaction at a time but does not compose (multi-surface
// grouped drag, post-release settle, ambient ticks all need to share one beat).
//
// FrameClock owns:
//   - one hidden message-only HWND that hosts the timer
//   - a subscriber set keyed by opaque token
//   - lazy start: the timer only runs while subscribers > 0 (zero idle cost)
//   - per-tick wall delta (dt_ms) passed to each subscriber
//
// Subscribers are UI-thread, synchronous. The clock is NOT vsync-aligned in 7C
// (that requires DwmGetCompositionTimingInfo or DComp); it is a USER timer at
// ~60 Hz cadence. Sub-16ms precision would need timeBeginPeriod(1) or a
// multimedia/queue timer — deliberately out of scope.
//
// PHASE 8B.7 — RUNTIME TEMPORAL AUTHORITY (dual path). FrameClock keeps its
// subscriber registry and lazy-idle accounting, but can now be EXTERNALLY DRIVEN:
// when the FrameScheduler owns the outer loop, it calls PumpEpoch(dt_ms) once per
// epoch and the clock does NOT run its own SetTimer. The subscriber CONTRACT is
// unchanged — Subscribe/Unsubscribe and the void(dt_ms) callback are identical in
// both modes, so InteractionSession / TickMonitor / SoakRunner / harness one-shots
// require ZERO changes. In legacy mode (external_clock_ == false) the WM_TIMER
// path is exactly as in 7C.
class FrameClock {
 public:
  using Subscriber = std::function<void(double dt_ms)>;
  using Token = int;

  static constexpr UINT kTickIntervalMs = 16;          // ~60 Hz target
  static constexpr UINT_PTR kClockTimerId = 0x4D434C4B; // 'MCLK'

  FrameClock();
  ~FrameClock();

  FrameClock(const FrameClock&) = delete;
  FrameClock& operator=(const FrameClock&) = delete;

  // Subscribe. The clock starts ticking on the FIRST subscriber. Returns a
  // token used to Unsubscribe; tokens are NEVER reused so a stale Unsubscribe
  // is a safe no-op.
  Token Subscribe(Subscriber subscriber);
  void Unsubscribe(Token token);

  size_t subscriber_count() const { return subscribers_.size(); }
  bool running() const { return running_; }

  // PHASE 8B.7 — switch cadence ownership. Call BEFORE any subscriber arrives
  // (i.e. at runtime construction). When externally_driven == true, the clock
  // never calls SetTimer; the FrameScheduler drives it via PumpEpoch. When false
  // (default), the clock owns its own WM_TIMER exactly as in 7C. Switching while
  // running is supported defensively (Start/Stop respect the flag) but the
  // intended use is set-once at boot — pump-strategy is foundational state.
  void SetExternallyDriven(bool externally_driven);
  bool externally_driven() const { return external_clock_; }

  // PHASE 8B.7 — the scheduler's RuntimeUpdate phase calls this once per epoch
  // with the epoch-to-epoch wall delta. It runs the SAME snapshot-dispatch body
  // as the WM_TIMER OnTimer (so subscriber semantics are identical), but takes
  // dt_ms from the caller instead of self-computing from last_tick_. No-op when
  // there are no subscribers (preserves the zero-idle property).
  void PumpEpoch(double dt_ms);

 private:
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM) noexcept;
  void OnTimer();
  void Dispatch(double dt_ms);  // shared snapshot-dispatch body (OnTimer + PumpEpoch)
  void Start();
  void Stop();

  HWND hwnd_ = nullptr;  // hidden message-only window owning the timer
  Token next_token_ = 1;
  std::unordered_map<Token, Subscriber> subscribers_;
  LARGE_INTEGER last_tick_{};
  bool running_ = false;
  // PHASE 8B.7 — when true the scheduler owns cadence; no internal SetTimer.
  bool external_clock_ = false;
};

#endif  // RUNNER_FRAME_CLOCK_H_
