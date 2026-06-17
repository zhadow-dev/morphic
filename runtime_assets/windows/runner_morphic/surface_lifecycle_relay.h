#ifndef RUNNER_SURFACE_LIFECYCLE_RELAY_H_
#define RUNNER_SURFACE_LIFECYCLE_RELAY_H_

#include <string>

#include "runtime_events.h"

class EventBus;
class SurfaceManager;
class SurfaceShell;

// M2.1D — SurfaceLifecycleRelay: projects GENERIC runtime surface facts onto the app bus.
//
// THE RELAY LAYER (hard constraint): runtime lifecycle events must NEVER mutate app state directly
// — that would make the runtime application-aware by accident. This is the ONE bridge. It
// subscribes to the runtime EventBus and re-broadcasts OPAQUE, app-agnostic facts onto every
// surface's `morphic/app` channel as `app.event {topic, payload:{surfaceId}}`:
//   • surface.created   {surfaceId}  — broadcast to all
//   • surface.destroyed {surfaceId}  — broadcast to all (apps map surface→meaning + clean up)
//   • surface.identity  {surfaceId}  — delivered to THAT surface (it learns its own id)
//
// It carries ONLY opaque semantic ids (e.g. "workspace_3") — never HWNDs, engine handles, native
// pointers, or SurfaceKind. The app composes meaning UPWARD; the runtime never learns
// "doc"/"note"/"inspector". This is the foundational addressing primitive (lifecycle cleanup +
// targeted orchestration both demanded it).
class SurfaceLifecycleRelay {
 public:
  SurfaceLifecycleRelay(EventBus* bus, SurfaceManager* manager);
  ~SurfaceLifecycleRelay();

  SurfaceLifecycleRelay(const SurfaceLifecycleRelay&) = delete;
  SurfaceLifecycleRelay& operator=(const SurfaceLifecycleRelay&) = delete;

 private:
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);
  void BroadcastToAll(const char* topic, const std::string& surface_id);

  EventBus* bus_;          // not owned (runtime)
  SurfaceManager* manager_;  // not owned (runtime) — enumerate surfaces + messengers
  int bus_token_ = 0;
};

#endif  // RUNNER_SURFACE_LIFECYCLE_RELAY_H_
