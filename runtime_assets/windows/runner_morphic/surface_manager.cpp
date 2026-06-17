#include "surface_manager.h"

#include <windows.h>

#include <string>

#include "engine_state.h"
#include "experiment_config.h"
#include "forensic_trace.h"
#include "interaction_router.h"
#include "resource_counters.h"
#include "runtime_events.h"
#include "surface_model.h"
#include "window_topology.h"

SurfaceManager::~SurfaceManager() {
  // Gate re-entrant OnSurfaceDestroyed: destroying surfaces_ below triggers each
  // shell's WM_DESTROY → OnSurfaceDestroyed, which must NOT touch the vectors
  // being torn down.
  shutting_down_ = true;
}

bool SurfaceManager::CreateSurface(const flutter::DartProject& base_project,
                                   std::string id, const std::string& entrypoint,
                                   int x, int y, int width, int height,
                                   DWORD ex_style, HWND owner, bool chromeless) {
  Reap();  // safe point to delete any previously-closed surfaces
  forensic::Log("SURFACE", "CreateSurface id=" + id + " entrypoint=" + entrypoint);
  auto shell = std::make_unique<SurfaceShell>(this, std::move(id));
  // PHASE 10.3 — forward the caller-chosen Win32 ex-style + owner HWND (and the
  // SPATIAL CHROME chromeless flag) to the shell. These are plain Win32 values
  // handed down by the product policy layer; the manager (like the shell) never
  // learns SurfaceKind.
  if (!shell->Create(base_project, entrypoint, x, y, width, height, ex_style,
                     owner, chromeless)) {
    forensic::Log("SURFACE", "CreateSurface FAILED");
    return false;
  }

  SurfaceShell* raw = shell.get();
  if (model_) {
    model_->Add(raw);  // register with the semantic model (enters on top)
  }
  surfaces_.push_back(std::move(shell));
  if (bus_) {
    bus_->Publish(RuntimeEvent::SurfaceCreated, raw);
  }
  return true;
}

namespace {
long long NowQpc() {
  LARGE_INTEGER v{};
  QueryPerformanceCounter(&v);
  return v.QuadPart;
}
double MsBetween(long long a, long long b) {
  static long long freq = [] {
    LARGE_INTEGER f{};
    QueryPerformanceFrequency(&f);
    return f.QuadPart ? f.QuadPart : 1;
  }();
  return static_cast<double>(b - a) * 1000.0 / static_cast<double>(freq);
}
// Paced/quiesced teardown tuning. Serialization (one engine destroyed at a time)
// suppresses the flutter_windows.dll teardown race but does NOT eliminate it.
// Dwell (quiesce) + gap (cooldown) are now ENV-TUNABLE (experiment_config.h) so a
// sweep harness can characterize their effect on crash incidence STATISTICALLY;
// these are the defaults when no MORPHIC_TD_* env is set.
constexpr double kStateDeadlineMs = 5000;  // watchdog: a state must not overstay

// DRAIN strategy: pump the message loop just before the engine reset so any
// pending engine platform-thread tasks are serviced (the worker-thread shutdown
// race may be a task left in flight). Re-entrancy is fenced by Reaping(): the
// frame-clock tick this may re-dispatch calls TickTeardown + the churn step,
// both of which no-op while a reap is in flight, so the in-flight teardown_
// entry + our iteration stay valid.
void DrainMessages(int max_msgs) {
  MSG msg;
  int n = 0;
  while (n < max_msgs && PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      PostQuitMessage(static_cast<int>(msg.wParam));  // don't swallow quit
      break;
    }
    TranslateMessage(&msg);
    DispatchMessage(&msg);
    ++n;
  }
}
}  // namespace

bool SurfaceManager::DestroySurfaceById(const std::string& id) {
  // DEFERRED TEARDOWN: move the surface out of the active set NOW (visual close
  // is immediate via BeginTeardown's SurfaceDestroyed publish + the sibling
  // topology), and park the engine for paced/quiesced destruction. Do NOT
  // DestroyWindow synchronously — that is the flutter_windows.dll teardown race.
  for (auto it = surfaces_.begin(); it != surfaces_.end(); ++it) {
    if ((*it)->id() == id) {
      std::unique_ptr<SurfaceShell> shell = std::move(*it);
      surfaces_.erase(it);
      forensic::Log("SURFACE", "DestroySurfaceById id=" + id + " (deferred)");
      BeginTeardown(std::move(shell));
      return true;
    }
  }
  return false;
}

void SurfaceManager::SetState(TeardownEntry& e, morphic::TeardownState to) {
  forensic::Log("TEARDOWN", "id=" + e.id + " " +
                                morphic::TeardownStateName(e.state) + " -> " +
                                morphic::TeardownStateName(to));
  e.state = to;
  e.state_since_qpc = NowQpc();
}

void SurfaceManager::BeginTeardown(std::unique_ptr<SurfaceShell> shell) {
  if (!shell) return;
  SurfaceShell* raw = shell.get();
  const std::string id = raw->id();

  // Semantic + visual detach NOW — the surface is logically gone. Subscribers
  // (compositor drops the host [engine survives: sibling of host], scene mirror,
  // lifecycle relay) see it destroyed immediately; the ENGINE is parked.
  if (router_) router_->CancelInteraction(raw, "close");
  if (model_) model_->Remove(raw);
  if (bus_) bus_->Publish(RuntimeEvent::SurfaceDestroyed, raw);

  if constexpr (morphic::kEngineRetention) {
    // R1 — RETAIN the engine instead of destroying it. The visual/semantic
    // detach above already made the surface logically gone; the engine becomes
    // DORMANT (no reset = no teardown-race crash). Destroys happen only under
    // pool pressure (EnforceDormantCap) or never (R0 abrupt exit).
    MakeDormant(std::move(shell));
  } else {
    TeardownEntry e;  // legacy destroy-on-close path
    e.id = id;
    e.shell = std::move(shell);
    e.begin_qpc = NowQpc();
    e.state = morphic::TeardownState::kVisuallyRemoved;
    e.state_since_qpc = e.begin_qpc;
    forensic::Log("TEARDOWN",
                  "id=" + id + " ACTIVE -> DETACHING -> VISUALLY_REMOVED");
    teardown_.push_back(std::move(e));
  }

  forensic::Log("LIFETIME", "surface closed -> active remaining=" +
                                std::to_string(surfaces_.size()) +
                                " (dormant=" + std::to_string(dormant_.size()) +
                                " teardown=" + std::to_string(teardown_.size()) +
                                ")");
  if (surfaces_.empty() && on_empty_) on_empty_();
}

long SurfaceManager::DormantCap() const {
  const long c = morphic::experiment::Get().dormant_cap;
  return c > 0 ? c : 1;
}

void SurfaceManager::EnsurePoolOwner() {
  if (pool_owner_ != nullptr) return;
  static const wchar_t* kClass = L"MORPHIC_POOL_OWNER";
  static bool registered = false;
  HINSTANCE inst = GetModuleHandle(nullptr);
  if (!registered) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = inst;
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);
    registered = true;
  }
  // Pure infrastructure: a hidden, unowned tool window — the "graveyard root"
  // that owns dormant render_sources so they survive workspace teardown and stay
  // off-shell. NEVER shown. Not a shell/preview/activation citizen.
  pool_owner_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, L"MorphicEnginePool",
                                WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst,
                                nullptr);
  if (pool_owner_ == nullptr) {
    forensic::Log("POOL FAIL", "pool_owner CreateWindowEx err=" +
                                   std::to_string(GetLastError()));
    return;
  }
  morphic::topo::ApplyRolePolicy(pool_owner_,
                                 morphic::topo::WindowRole::kPoolOwner, nullptr);
  forensic::Log("POOL",
                "pool_owner created (process-owned engine graveyard root)");
}

void SurfaceManager::MakeDormant(std::unique_ptr<SurfaceShell> shell) {
  if (!shell) return;
  EnsurePoolOwner();
  const std::string id = shell->id();
  HWND engine = shell->GetHandle();
  // Re-parent the render_source to the pool_owner and re-assert render_source
  // policy (TOOLWINDOW|NOACTIVATE, cloaked, peek-excluded) so a DORMANT engine is
  // semantically dead: off-shell, no Alt-Tab/preview/activation. Hide it so
  // Flutter suppresses presents (no rendering while dormant). NOT reset, NOT
  // sanitized — a suspended original (DORMANT != REUSABLE).
  if (engine != nullptr && pool_owner_ != nullptr) {
    morphic::topo::ApplyRolePolicy(
        engine, morphic::topo::WindowRole::kRenderSource, pool_owner_);
    ShowWindow(engine, SW_HIDE);
    // Invariant: a DORMANT engine must NEVER be a shell citizen (Alt-Tab/taskbar).
    if (morphic::topo::IsShellEligible(engine)) {
      forensic::Log("POOL FAIL",
                    "DORMANT engine is shell-eligible id=" + id +
                        " (must be off-shell) — invariant breach");
    }
  }
  DormantEngine d;
  d.id = id;
  d.shell = std::move(shell);
  d.since_qpc = NowQpc();
  d.state = morphic::EngineState::kDormant;
  dormant_.push_back(std::move(d));
  ++dormant_retained_total_;
  forensic::Log("POOL", "id=" + id +
                            " LIVE -> DORMANT (engine retained, NOT destroyed)"
                            " dormant=" +
                            std::to_string(dormant_.size()) + " cap=" +
                            std::to_string(DormantCap()) + " retained_total=" +
                            std::to_string(dormant_retained_total_));
  EnforceDormantCap();
}

void SurfaceManager::EnforceDormantCap() {
  const long cap = DormantCap();
  while (static_cast<long>(dormant_.size()) > cap) {
    // Evict the OLDEST (front). This is the ONLY runtime destroy now — rare,
    // bounded by pressure. Route through the serialized/paced reaper so the
    // flutter_windows teardown race stays serialized.
    DormantEngine victim = std::move(dormant_.front());
    dormant_.erase(dormant_.begin());
    ++dormant_evicted_total_;
    forensic::Log("POOL",
                  "id=" + victim.id +
                      " DORMANT -> RETIRING (cap pressure: evict oldest) dormant=" +
                      std::to_string(dormant_.size()) + " evicted_total=" +
                      std::to_string(dormant_evicted_total_));
    TeardownEntry e;
    e.id = victim.id;
    e.shell = std::move(victim.shell);
    e.begin_qpc = NowQpc();
    e.state = morphic::TeardownState::kReapPending;  // straight to the destroy slot
    e.state_since_qpc = e.begin_qpc;
    teardown_.push_back(std::move(e));
  }
}

void SurfaceManager::MaybeLogPoolMetrics() {
  if (dormant_.empty() && dormant_retained_total_ == 0) return;
  const long long now = NowQpc();
  if (pool_metrics_last_qpc_ != 0 &&
      MsBetween(pool_metrics_last_qpc_, now) < 5000.0) {
    return;  // throttle ~5s
  }
  pool_metrics_last_qpc_ = now;
  // Retention creep is the new operational enemy — make it observable. Process
  // commit + handles/GDI/USER from the resource sampler; oldest-dormant age.
  const morphic::res::Snapshot s = morphic::res::Capture(
      static_cast<long>(dormant_.size()), /*dxgi_adapter3=*/nullptr);
  double oldest_ms = 0;
  if (!dormant_.empty()) oldest_ms = MsBetween(dormant_.front().since_qpc, now);
  forensic::Log(
      "POOL METRICS",
      "dormant=" + std::to_string(dormant_.size()) + "/" +
          std::to_string(DormantCap()) + " retained_total=" +
          std::to_string(dormant_retained_total_) + " evicted_total=" +
          std::to_string(dormant_evicted_total_) + " oldest_age=" +
          std::to_string(static_cast<long>(oldest_ms / 1000)) + "s | commit=" +
          std::to_string(s.private_kb) + "KB handles=" +
          std::to_string(s.handles) + " gdi=" + std::to_string(s.gdi) +
          " user=" + std::to_string(s.user) + " hwnds=" +
          std::to_string(s.hwnds));
}

size_t SurfaceManager::TickTeardown() {
  // Re-entrancy fence (see DrainMessages): if a reap is mid-flight and the
  // message pump re-dispatched the frame tick, do nothing — the outer reap owns
  // teardown_ right now.
  if (morphic::experiment::Reaping().load()) return teardown_.size();
  MaybeLogPoolMetrics();  // retention-creep observability (runs even when idle)
  if (teardown_.empty()) return 0;
  const morphic::experiment::Config& cfg = morphic::experiment::Get();
  const long long now = NowQpc();

  // Advance the light (free) states + watchdog.
  for (auto& e : teardown_) {
    const double age = MsBetween(e.state_since_qpc, now);
    if (age > kStateDeadlineMs && e.state != morphic::TeardownState::kDestroyed) {
      forensic::Log("TEARDOWN WATCHDOG",
                    "id=" + e.id + " STUCK in " +
                        morphic::TeardownStateName(e.state) + " for " +
                        std::to_string(static_cast<long>(age)) + "ms");
      e.state_since_qpc = now;  // log once per deadline, don't spam
    }
    if (e.state == morphic::TeardownState::kVisuallyRemoved) {
      SetState(e, morphic::TeardownState::kQuiescing);
    } else if (e.state == morphic::TeardownState::kQuiescing &&
               age >= cfg.td_quiesce_ms) {
      SetState(e, morphic::TeardownState::kReapPending);
    }
  }

  // SERIALIZED + PACED heavy teardown: at most one engine destroyed per cooldown.
  if (reap_cooldown_ > 0) {
    --reap_cooldown_;
  } else {
    for (auto& e : teardown_) {
      if (e.state != morphic::TeardownState::kReapPending) continue;
      SetState(e, morphic::TeardownState::kDestroying);

      // LEDGER — the survival record. Emitted BEFORE the destructive call so the
      // crash dump's LAST 'attempt' line (no matching 'survived') names the exact
      // teardown that hit the race, with the conditions it died under.
      const long seq = ++teardown_seq_;
      if (cfg.td_ledger) {
        forensic::Log("TDSTAT",
                      "attempt run=" + std::to_string(cfg.run_id) + " cfg=" +
                          cfg.td_name + " seq=" + std::to_string(seq) + " id=" +
                          e.id + " active=" +
                          std::to_string(surfaces_.size()) + " queue=" +
                          std::to_string(teardown_.size()));
      }

      // Fence re-entrancy across the whole destructive section (ReleaseEngine may
      // pump internally; the optional drain definitely does).
      morphic::experiment::Reaping().store(true);
      if (cfg.td_drain_pump) DrainMessages(64);
      e.shell->ReleaseEngine();  // FlutterViewController dtor — the race surface
      if (cfg.td_drain_pump) DrainMessages(64);  // service shutdown-posted tasks
      if (HWND h = e.shell->GetHandle()) DestroyWindow(h);
      morphic::experiment::Reaping().store(false);

      SetState(e, morphic::TeardownState::kDestroyed);
      if (cfg.td_ledger) {
        forensic::Log("TDSTAT", "survived run=" + std::to_string(cfg.run_id) +
                                    " cfg=" + cfg.td_name + " seq=" +
                                    std::to_string(seq));
      }
      reap_cooldown_ = cfg.td_cooldown_ticks;
      break;  // ONE at a time
    }
  }

  // Free DESTROYED entries + record metrics.
  for (auto it = teardown_.begin(); it != teardown_.end();) {
    if (it->state == morphic::TeardownState::kDestroyed) {
      const double total = MsBetween(it->begin_qpc, NowQpc());
      ++teardowns_completed_;
      teardown_latency_sum_ms_ += total;
      if (total > teardown_latency_max_ms_) teardown_latency_max_ms_ = total;
      forensic::Log("TEARDOWN",
                    "id=" + it->id + " freed (latency=" +
                        std::to_string(static_cast<long>(total)) +
                        "ms avg=" +
                        std::to_string(static_cast<long>(
                            teardown_latency_sum_ms_ / teardowns_completed_)) +
                        "ms max=" +
                        std::to_string(static_cast<long>(teardown_latency_max_ms_)) +
                        "ms queue=" + std::to_string(teardown_.size() - 1) + ")");
      it = teardown_.erase(it);
    } else {
      ++it;
    }
  }
  return teardown_.size();
}

SurfaceShell* SurfaceManager::FindById(const std::string& id) const {
  for (const auto& s : surfaces_) {
    if (s->id() == id) return s.get();
  }
  return nullptr;
}

void SurfaceManager::OnSurfaceDestroyed(SurfaceShell* surface) {
  if (shutting_down_) {
    return;  // teardown: the vectors clean themselves up
  }

  // REENTRANCY-SAFE / IDEMPOTENT (use-after-free guard — found by the churn
  // harness). The host owns the engine, so Publish -> compositor Drop ->
  // DestroyWindow(host) cascades a SECOND WM_DESTROY -> reentrant
  // OnSurfaceDestroyed for the same surface. We must move the surface OUT of
  // surfaces_ (to the graveyard) BEFORE publishing, so that reentrant call finds
  // it gone and bails — no double publish, no use-after-free. `surface` stays
  // alive in the graveyard for this synchronous call; Reap() frees it later.
  std::unique_ptr<SurfaceShell> owned;
  for (auto it = surfaces_.begin(); it != surfaces_.end(); ++it) {
    if (it->get() == surface) {
      owned = std::move(*it);
      surfaces_.erase(it);
      break;
    }
  }
  if (!owned) {
    forensic::Log("SURFACE", "OnSurfaceDestroyed ignored (already removed)");
    return;
  }
  graveyard_.push_back(std::move(owned));

  if (model_) {
    model_->Remove(surface);  // unregister from the semantic model
  }

  forensic::Log("SURFACE", "OnSurfaceDestroyed id=" + surface->id());
  if (bus_) {
    bus_->Publish(RuntimeEvent::SurfaceDestroyed, surface);
  }

  // TEARDOWN TELEMETRY — surface count is the (pre-inversion) lifetime signal.
  // Post-inversion this is the render_source count, NOT workspace lifetime; a
  // single close emptying this to 0 (e.g. a parent-cascade) must NOT be allowed
  // to terminate the workspace. See doc/WINDOW_TOPOLOGY.md lifecycle rules.
  forensic::Log("LIFETIME", "surface destroyed -> remaining=" +
                                std::to_string(surfaces_.size()));

  // App-lifetime policy is NOT decided here (F8) — delegate to the owner.
  if (surfaces_.empty() && on_empty_) {
    forensic::Log("LIFETIME",
                  "surface vector EMPTY -> notifying lifetime owner (on_empty)");
    on_empty_();
  }
}

void SurfaceManager::OnSurfaceReady(SurfaceShell* surface) {
  // M2.1D — generic "engine up" fact; the lifecycle relay turns it into a per-surface identity
  // push. No app/kind awareness here.
  if (bus_ && surface) {
    bus_->Publish(RuntimeEvent::SurfaceReady, surface);
  }
}

void SurfaceManager::Reap() {
  if (!graveyard_.empty()) {
    forensic::Log("SURFACE", "Reap: freeing " +
                                 std::to_string(graveyard_.size()) +
                                 " closed surface(s)");
    graveyard_.clear();
  }
}
