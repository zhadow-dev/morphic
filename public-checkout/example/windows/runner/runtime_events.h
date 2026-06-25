#ifndef RUNNER_RUNTIME_EVENTS_H_
#define RUNNER_RUNTIME_EVENTS_H_

#include <functional>
#include <unordered_map>
#include <vector>

class SurfaceShell;

// PHASE: stabilization. A minimal, synchronous, UI-THREAD-ONLY runtime event
// bus. Replaces imperative side-effects scattered through WndProc with
// observable events. No async, no cross-thread delivery yet (see
// CORE_HARDENING.md §6) — Flutter raster threads must NOT publish here.
enum class RuntimeEvent {
  SurfaceCreated,
  // M2.1D — the surface's Flutter engine rendered its first frame (Dart is up and can receive a
  // pushed message). Used by SurfaceLifecycleRelay to deliver the surface its own identity.
  SurfaceReady,
  SurfaceActivated,
  SurfaceFocused,
  SurfaceMoved,
  SurfaceResized,
  InteractionBegan,
  InteractionUpdated,
  InteractionEnded,
  // PHASE 11 — DEFERRED TEARDOWN. SurfaceClosing fires at close REQUEST: the
  // visual must go immediately (compositor drops the host, reconciler
  // unregisters) while the engine is parked for paced/quiesced teardown.
  // SurfaceDestroyed now fires only when the engine HWND is ACTUALLY destroyed
  // by the reaper (the terminal step). Visual lifetime != engine lifetime.
  SurfaceClosing,
  SurfaceDestroyed,
  // PHASE 8A — surface graph topology mutations. Payload `surface` is the
  // DEPENDENT for Attached/Detached (the side that gained/lost an anchor);
  // for Grouped/Ungrouped it's ANY one member of the affected group (the
  // group id itself isn't carried by the current bus; consumers query the
  // graph for context). 8B+ will add structured payloads if needed.
  SurfaceAttached,
  SurfaceDetached,
  SurfaceGrouped,
  SurfaceUngrouped,
  // PHASE 8C — live topology fracture (extraction). TopologyMutated is a PRE-state
  // event: it fires BEFORE the graph mutation so observers can still read the
  // pre-fracture topology (payload = the member about to leave). GroupFractured
  // fires AFTER (payload = the leader whose group lost a member); SurfaceDetached
  // (existing) fires AFTER for the extracted member.
  TopologyMutated,
  GroupFractured,
  // PHASE 8D — semantic docking (membership ADD, inverse of 8C fracture). When the
  // dragged SOURCE surface converges (proximity + overlap, sustained) onto a
  // detached TARGET, the epoch boundary forms a group. TopologyMutated fires PRE
  // (payload = source), then the graph groups them, then SurfaceDocked fires POST
  // (payload = source). The group is queryable via SurfaceGraph::GroupOf.
  // SurfaceUndocked is reserved for an explicit-undock path; in 8D, ungrouping a
  // docked pair already fires SurfaceUngrouped, so SurfaceUndocked may be
  // declared-but-unpublished this phase (kept for symmetry).
  SurfaceDocked,
  SurfaceUndocked,
};

const char* ToString(RuntimeEvent event);

class EventBus {
 public:
  using Handler = std::function<void(RuntimeEvent, SurfaceShell*)>;
  // 0 = invalid sentinel (Unsubscribe(0) is a safe no-op).
  using Token = int;

  // PHASE 8B-prep — token-based subscription closes invariant I-A2. Subscribers
  // whose handlers capture `this` MUST store the returned token and call
  // Unsubscribe in their destructor to avoid publish-into-freed-capture.
  // Subscribers whose handlers capture nothing live for the bus's lifetime.
  Token Subscribe(Handler handler);
  void Unsubscribe(Token token);

  // Publish synchronously on the calling (UI) thread. Re-entrant Subscribe /
  // Unsubscribe from within a handler are safe (Publish iterates a snapshot
  // copy of the handler set).
  void Publish(RuntimeEvent event, SurfaceShell* surface);

  size_t subscriber_count() const { return handlers_.size(); }

 private:
  Token next_token_ = 1;
  std::unordered_map<Token, Handler> handlers_;
};

#endif  // RUNNER_RUNTIME_EVENTS_H_
