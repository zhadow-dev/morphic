#ifndef RUNNER_SURFACE_MANAGER_H_
#define RUNNER_SURFACE_MANAGER_H_

#include <flutter/dart_project.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "engine_state.h"
#include "surface_shell.h"
#include "teardown_state.h"

class InteractionRouter;
class SurfaceModel;
class EventBus;

// PHASE 2 / stabilization — SurfaceManager
//
// Owns surface LIFECYCLE only: creation, ownership (unique_ptr), and safe
// teardown. Semantic state (z-order, activation, focus, visibility) now lives
// in SurfaceModel (CORE_HARDENING §5); the manager merely registers/unregisters
// surfaces with it. The manager holds (non-owning) pointers to the model and
// router so SurfaceShell can reach them via its manager back-pointer.
//
// Lifecycle correctness (F6/F7/F8):
//   - OnSurfaceDestroyed runs INSIDE the dying surface's WndProc, so the object
//     is parked in graveyard_ and freed later by Reap().
//   - shutting_down_ gates re-entrant OnSurfaceDestroyed during teardown.
//   - Quit policy is delegated via on_empty_ (no PostQuitMessage here).
class SurfaceManager {
 public:
  using EmptyCallback = std::function<void()>;

  SurfaceManager() = default;
  ~SurfaceManager();

  SurfaceManager(const SurfaceManager&) = delete;
  SurfaceManager& operator=(const SurfaceManager&) = delete;

  // Wiring — set once at startup by MorphicRuntime.
  void SetModel(SurfaceModel* model) { model_ = model; }
  void SetRouter(InteractionRouter* router) { router_ = router; }
  void SetEventBus(EventBus* bus) { bus_ = bus; }
  void SetOnEmpty(EmptyCallback cb) { on_empty_ = std::move(cb); }

  SurfaceModel* model() const { return model_; }
  InteractionRouter* router() const { return router_; }
  EventBus* bus() const { return bus_; }
  size_t SurfaceCount() const { return surfaces_.size(); }

  // PHASE 10.3 — `ex_style` (default WS_EX_APPWINDOW) + `owner` (default null) are
  // PLAIN Win32 values forwarded to SurfaceShell::Create. The product policy layer
  // resolves SurfaceKind→native behavior and passes the concrete flags here; the
  // manager stays surface-type-AGNOSTIC (firewall: arrow points DOWN only).
  // SPATIAL CHROME — `chromeless` (no strip/borders, full-bleed child) follows
  // the same pattern.
  bool CreateSurface(const flutter::DartProject& base_project, std::string id,
                     const std::string& entrypoint, int x, int y, int width,
                     int height, DWORD ex_style = WS_EX_APPWINDOW,
                     HWND owner = nullptr, bool chromeless = false);

  // PHASE 10.2 — destroy a surface by its ecology id. Posts DestroyWindow which
  // triggers the existing WM_DESTROY → OnSurfaceDestroyed flow. Returns true if
  // the surface was found and destroyed.
  bool DestroySurfaceById(const std::string& id);

  // PHASE 10.2 — find a surface by id. Returns nullptr if absent.
  SurfaceShell* FindById(const std::string& id) const;

  // SPATIAL MIGRATION Stage 2A — enumerate LIVE surfaces (excludes graveyard) for
  // product-layer projection. Read-only/neutral (like FindById / SurfaceCount); the
  // manager stays surface-type-agnostic.
  std::vector<SurfaceShell*> Surfaces() const {
    std::vector<SurfaceShell*> out;
    out.reserve(surfaces_.size());
    for (const auto& s : surfaces_) out.push_back(s.get());
    return out;
  }

  // A surface's window was destroyed (from its WM_DESTROY). Unregisters it from
  // the model, defers object deletion, and notifies the lifetime owner if empty.
  void OnSurfaceDestroyed(SurfaceShell* surface);

  // M2.1D — the surface's Flutter engine rendered its first frame (called from the shell's
  // first-frame callback). Publishes SurfaceReady (generic) so SurfaceLifecycleRelay can hand the
  // surface its own opaque id. Pure runtime fact — no app/kind semantics.
  void OnSurfaceReady(SurfaceShell* surface);

  // Free graveyard_ (closed surfaces -> their FlutterViewControllers/engines).
  // Driven from the runtime's frame tick — a safe point OUTSIDE any dying
  // surface's WM_DESTROY dispatch. Cheap no-op when the graveyard is empty.
  void Reap();

  // PHASE 11 — DEFERRED/QUIESCED TEARDOWN. Drive the teardown state machine one
  // step on the frame tick: advance each queued surface through QUIESCING ->
  // REAP_PENDING -> DESTROYING (serialized: at most one DESTROYING at a time),
  // with a watchdog on overstayed states. Returns the live teardown-queue depth.
  size_t TickTeardown();

 private:
  // Move [surface] out of surfaces_ into the deferred-teardown queue: the visual
  // is removed NOW (SurfaceClosing -> compositor drops the host), the engine is
  // parked for paced destruction. Engine lifetime != visual lifetime.
  void BeginTeardown(std::unique_ptr<SurfaceShell> surface);

  struct TeardownEntry {
    std::unique_ptr<SurfaceShell> shell;
    std::string id;
    morphic::TeardownState state = morphic::TeardownState::kVisuallyRemoved;
    long long state_since_qpc = 0;  // for watchdog + latency
    long long begin_qpc = 0;        // total teardown latency
  };
  void SetState(TeardownEntry& e, morphic::TeardownState to);

  // ENGINE RETENTION — PHASE R1. Destruction is the hazard (R0 finding), so a
  // closed surface's engine is RETAINED dormant, not destroyed. Destroys happen
  // ONLY under pool-pressure eviction (fixed cap), routed back through the
  // serialized reaper. NO reuse, NO reset, NO sanitization (that is R2). Dormant
  // engines re-parent to a process-owned pool_owner and stay semantically dead.
  struct DormantEngine {
    std::unique_ptr<SurfaceShell> shell;
    std::string id;
    long long since_qpc = 0;  // retention duration
    morphic::EngineState state = morphic::EngineState::kDormant;
  };
  void EnsurePoolOwner();                              // lazy graveyard-root HWND
  void MakeDormant(std::unique_ptr<SurfaceShell> sh);  // LIVE -> DORMANT (retain)
  void EnforceDormantCap();                            // evict oldest over cap
  long DormantCap() const;                             // fixed cap (env-overridable)
  void MaybeLogPoolMetrics();                          // throttled retention telemetry

  bool shutting_down_ = false;
  SurfaceModel* model_ = nullptr;        // not owned — semantic authority
  InteractionRouter* router_ = nullptr;  // not owned
  EventBus* bus_ = nullptr;              // not owned
  EmptyCallback on_empty_;
  std::vector<std::unique_ptr<SurfaceShell>> graveyard_;  // awaiting Reap()
  std::vector<std::unique_ptr<SurfaceShell>> surfaces_;   // ownership
  std::vector<TeardownEntry> teardown_;  // deferred/quiesced engine teardown

  long reap_cooldown_ = 0;  // ticks until the next serialized engine destroy
  long teardown_seq_ = 0;   // per-process destroy attempt counter (ledger key)

  // Engine retention pool (R1).
  HWND pool_owner_ = nullptr;             // process-owned graveyard root
  std::vector<DormantEngine> dormant_;    // retained engines (capped)
  long dormant_retained_total_ = 0;       // lifetime retentions
  long dormant_evicted_total_ = 0;        // lifetime pressure-evictions
  long long pool_metrics_last_qpc_ = 0;   // throttle

  // teardown metrics (Phase 11 stress observability).
  long teardowns_completed_ = 0;
  double teardown_latency_sum_ms_ = 0;
  double teardown_latency_max_ms_ = 0;
};

#endif  // RUNNER_SURFACE_MANAGER_H_
