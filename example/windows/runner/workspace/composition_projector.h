#ifndef RUNNER_WORKSPACE_COMPOSITION_PROJECTOR_H_
#define RUNNER_WORKSPACE_COMPOSITION_PROJECTOR_H_

#include <windows.h>

#include <string>
#include <unordered_map>

#include "runtime_events.h"  // EventBus + Token

class EventBus;
class SurfaceManager;
class SurfaceModel;
class SurfaceShell;

// PHASE 12D — CompositionProjector.
//
// The shared-drag MOVEMENT projector. Watches a composition plane ROOT's drag and
// re-projects its composed members so they ride along. Lives ENTIRELY ABOVE the runtime
// (Q7): it subscribes to the bus and uses only the PUBLIC SurfaceModel::SetBounds /
// bounds + SurfaceManager::FindById — it NEVER touches InteractionSession / SurfaceGraph
// / epochs, so composition can't pollute topology/interaction (firewall holds; the
// runtime stays composition-agnostic).
//
// Determinism: offsets are captured at drag START (InteractionBegan) — member - root —
// and members are re-projected at root + offset every InteractionUpdated. Capturing
// fresh per drag means a member you moved independently simply keeps its new relative
// position the next time the root is dragged (no stale offsets, no snap-back).
//
// Loop guard (Q9, product-side only): `projecting_` blocks re-entrancy and we react ONLY
// to plane roots — a member's own move never triggers a reprojection. No ProjectionOrigin
// leaks into RuntimeEvent (the runtime never learns about composition).
namespace morphic::workspace {

class WorkspaceCompositionGraph;

class CompositionProjector {
 public:
  // Compile-time kill switch (default true; false = no shared-drag movement, instant
  // rollback — like the other kEnable* toggles).
  static constexpr bool kEnableCompositionMovement = true;

  CompositionProjector(SurfaceModel* model, SurfaceManager* manager, EventBus* bus,
                       WorkspaceCompositionGraph* graph);
  ~CompositionProjector();

  CompositionProjector(const CompositionProjector&) = delete;
  CompositionProjector& operator=(const CompositionProjector&) = delete;

 private:
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);
  void CaptureOffsets(const std::string& root_id);
  void Reproject(const std::string& root_id);

  SurfaceModel* model_;       // not owned (runtime) — read bounds + SetBounds (public)
  SurfaceManager* manager_;   // not owned (runtime) — id → SurfaceShell*
  EventBus* bus_;             // not owned (runtime)
  WorkspaceCompositionGraph* graph_;  // not owned (product) — plane membership

  EventBus::Token bus_token_ = 0;
  bool projecting_ = false;            // re-entrancy guard
  std::string active_root_;            // the plane root currently being dragged
  std::unordered_map<std::string, POINT> offsets_;  // member_id -> (member - root) @ start
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_COMPOSITION_PROJECTOR_H_
