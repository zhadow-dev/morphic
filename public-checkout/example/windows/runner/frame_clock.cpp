#include "frame_clock.h"

#include <utility>
#include <vector>

#include "forensic_trace.h"

namespace {

constexpr const wchar_t kClassName[] = L"MORPHIC_FRAME_CLOCK";

double ElapsedMs(LARGE_INTEGER start, LARGE_INTEGER end) {
  LARGE_INTEGER freq{};
  QueryPerformanceFrequency(&freq);
  if (freq.QuadPart == 0) return 0.0;
  return (end.QuadPart - start.QuadPart) * 1000.0 /
         static_cast<double>(freq.QuadPart);
}

}  // namespace

FrameClock::FrameClock() {
  // Register the window class once. Inlined here (rather than an anonymous-
  // namespace helper) because WndProc is a private static member — only a
  // member function can take its address.
  static bool class_registered = false;
  if (!class_registered) {
    WNDCLASS wc{};
    wc.lpfnWndProc = &FrameClock::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kClassName;
    RegisterClass(&wc);
    class_registered = true;
  }
  // Message-only window: never visible, owned by HWND_MESSAGE, only purpose is
  // to host the SetTimer that drives the clock. Decoupling from any surface
  // HWND lets the clock outlive interactions, surface lifecycles, etc.
  hwnd_ = CreateWindowEx(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE,
                         nullptr, GetModuleHandle(nullptr), this);
  forensic::Log("CLOCK", "FrameClock created (message-only hwnd)");
}

FrameClock::~FrameClock() {
  Stop();
  if (hwnd_) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  forensic::Log("CLOCK", "FrameClock destroyed");
}

FrameClock::Token FrameClock::Subscribe(Subscriber subscriber) {
  const Token token = next_token_++;
  subscribers_.emplace(token, std::move(subscriber));
  forensic::Log("CLOCK", "Subscribe token=" + std::to_string(token) +
                            " count=" + std::to_string(subscribers_.size()));
  if (!running_) {
    Start();
  }
  return token;
}

void FrameClock::Unsubscribe(Token token) {
  auto erased = subscribers_.erase(token);
  if (erased == 0) {
    return;  // unknown / stale — safe no-op
  }
  forensic::Log("CLOCK", "Unsubscribe token=" + std::to_string(token) +
                            " count=" + std::to_string(subscribers_.size()));
  if (subscribers_.empty() && running_) {
    Stop();  // lazy idle — zero CPU when no one is listening
  }
}

void FrameClock::SetExternallyDriven(bool externally_driven) {
  if (external_clock_ == externally_driven) return;
  external_clock_ = externally_driven;
  forensic::Log("CLOCK", std::string("SetExternallyDriven=") +
                             (externally_driven ? "true (scheduler owns cadence)"
                                                : "false (WM_TIMER)"));
  // If we flip while running, reconcile the underlying timer: an externally
  // driven clock must not also keep a SetTimer alive, and a clock handed back
  // to legacy ownership while it has subscribers must start its own timer.
  if (running_ && hwnd_) {
    if (external_clock_) {
      KillTimer(hwnd_, kClockTimerId);
    } else {
      SetTimer(hwnd_, kClockTimerId, kTickIntervalMs, nullptr);
      last_tick_.QuadPart = 0;
    }
  }
}

void FrameClock::Start() {
  if (running_ || hwnd_ == nullptr) return;
  // PHASE 8B.7 — in externally driven mode the scheduler owns cadence; we keep
  // the lazy-idle subscriber accounting (running_ tracks "someone is listening")
  // but install NO SetTimer. PumpEpoch is the sole driver.
  if (!external_clock_) {
    SetTimer(hwnd_, kClockTimerId, kTickIntervalMs, nullptr);
  }
  last_tick_.QuadPart = 0;
  running_ = true;
  forensic::Log("CLOCK", std::string("Start (") +
                            (external_clock_ ? "external/scheduler-driven"
                                             : "interval=" +
                                                   std::to_string(kTickIntervalMs) +
                                                   "ms") +
                            ")");
}

void FrameClock::Stop() {
  if (!running_ || hwnd_ == nullptr) return;
  if (!external_clock_) {
    KillTimer(hwnd_, kClockTimerId);
  }
  running_ = false;
  forensic::Log("CLOCK", "Stop");
}

void FrameClock::OnTimer() {
  // Legacy (WM_TIMER) path: self-compute dt from the previous tick.
  LARGE_INTEGER now{};
  QueryPerformanceCounter(&now);
  double dt_ms = 0.0;
  if (last_tick_.QuadPart != 0) {
    dt_ms = ElapsedMs(last_tick_, now);
  }
  last_tick_ = now;
  Dispatch(dt_ms);
}

void FrameClock::PumpEpoch(double dt_ms) {
  // PHASE 8B.7 — scheduler-driven path. The caller already computed the epoch
  // delta; we just fan out to subscribers. No-op when nobody is listening so the
  // zero-idle property still holds (the scheduler still wakes on cadence, but a
  // subscriber-less PumpEpoch costs only the empty snapshot).
  if (subscribers_.empty()) return;
  Dispatch(dt_ms);
}

void FrameClock::Dispatch(double dt_ms) {
  // Snapshot TOKENS (not callbacks) so subscribers may Subscribe/Unsubscribe —
  // and even DESTROY themselves — during their own tick. Before invoking each,
  // we re-check the token still exists in the live map: if a subscriber's tick
  // unsubscribed another subscriber (e.g. the InteractionSimulator's tick calls
  // EndInteraction, which unsubscribes + destroys the InteractionEpoch), the
  // stale entry is SKIPPED rather than invoked through a dangling std::function
  // capturing a freed object (PHASE 8C UAF fix). Copying the callback into a
  // snapshot vector would NOT protect against this — the copy outlives the
  // freed capture. Token re-lookup is the correct guard.
  std::vector<Token> tokens;
  tokens.reserve(subscribers_.size());
  for (auto& [token, _] : subscribers_) {
    tokens.push_back(token);
  }
  for (Token token : tokens) {
    auto it = subscribers_.find(token);
    if (it == subscribers_.end()) {
      continue;  // unsubscribed (possibly self-destroyed) earlier this dispatch
    }
    // COPY the callback to a local before invoking. The callback may Subscribe/
    // Unsubscribe, which can REHASH subscribers_ and destroy/move the map node
    // (and the std::function it holds) WHILE operator() is on the stack — a UAF
    // if we called it->second(...) directly. The token re-lookup above skips
    // entries freed earlier this dispatch; the local copy keeps THIS one alive
    // for the duration of its own call.
    Subscriber cb = it->second;
    cb(dt_ms);
  }
}

LRESULT CALLBACK FrameClock::WndProc(HWND hwnd, UINT message, WPARAM wparam,
                                     LPARAM lparam) noexcept {
  if (message == WM_NCCREATE) {
    auto cs = reinterpret_cast<CREATESTRUCT*>(lparam);
    SetWindowLongPtr(hwnd, GWLP_USERDATA,
                     reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
  } else if (message == WM_TIMER && wparam == kClockTimerId) {
    if (auto* self = reinterpret_cast<FrameClock*>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA))) {
      self->OnTimer();
      return 0;
    }
  }
  return DefWindowProc(hwnd, message, wparam, lparam);
}
