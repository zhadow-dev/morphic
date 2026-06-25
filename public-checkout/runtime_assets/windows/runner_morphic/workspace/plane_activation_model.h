#ifndef RUNNER_WORKSPACE_PLANE_ACTIVATION_MODEL_H_
#define RUNNER_WORKSPACE_PLANE_ACTIVATION_MODEL_H_

#include <functional>
#include <string>

#include "runtime_events.h"  // EventBus + Token (runtime header — arrow points DOWN, allowed)
#include "spatial_config.h"  // R5-lite — toggle source of truth

class EventBus;
class SurfaceShell;

// SPATIAL MIGRATION — Stage 1: Plane activation (THE core inversion).
//
// Derives WHICH composition plane is "active" from the runtime's surface-level activation,
// WITHOUT the runtime ever learning what a plane is. This is the load-bearing migration: the
// plane (product layer) becomes the bearer of activation, the HWND stops being it.
//
// Shape (identical to CompositionProjector): OBSERVE runtime truth on the EventBus
// (SurfaceActivated / SurfaceFocused) → DERIVE the owning plane → record + log. It projects
// NOTHING in Stage 1 (no visuals, no native calls) — it only maintains truth and emits
// [PLANE] trace, so the semantics can be proven in isolation before Stage 2 renders them.
//
// CRITICAL LAW (see SPATIAL_RUNTIME_MIGRATION.md §4b): this is VISUAL/SEMANTIC activation
// ONLY. Foreground / keyboard / capture / IME / accessibility stay HWND-native. Nothing here
// routes input. A plane may be "active" while a different HWND owns the OS foreground — that is
// intended. This model must NEVER drive interaction; it only answers "which plane reads active".
//
// Firewall: uses WorkspaceCompositionGraph (product) for membership + EventBus / SurfaceShell
// (public runtime surfaces) for the signal. No runtime mutation; the runtime stays
// plane-agnostic. Lives entirely above the runtime, like the rest of workspace/.
namespace morphic::workspace {

class WorkspaceCompositionGraph;

class PlaneActivationModel {
 public:
  // Compile-time kill switch (default true; the model is inert when no planes exist, so
  // "on" is safe — it simply logs active=<none> until a composition plane is formed).
  static constexpr bool kEnablePlaneActivation =
      morphic::config::kEnablePlaneActivation;  // value: spatial_config.h

  PlaneActivationModel(WorkspaceCompositionGraph* graph, EventBus* bus);
  ~PlaneActivationModel();

  PlaneActivationModel(const PlaneActivationModel&) = delete;
  PlaneActivationModel& operator=(const PlaneActivationModel&) = delete;

  // Root id of the currently-active plane, or "" if the active surface belongs to no plane
  // (a free / floating surface). Stage 2's visual projector reads this.
  const std::string& active_root() const { return active_root_; }

  // The surface that last drove activation within the active plane (the "leader"), or the
  // free surface id when there is no active plane. Trace/diagnostic aid.
  const std::string& leader_surface_id() const { return leader_surface_id_; }

  // True iff `surface_id`'s plane is the active one. False for free surfaces and for members
  // of any non-active plane.
  bool IsSurfaceInActivePlane(const std::string& surface_id) const;

  // True iff `surface_id` is a MEMBER of some plane (not a root, not free). Used by the
  // visual projector to tell a member's content to adopt the plane's glass material. Roots
  // already define the plane material, so they return false here.
  bool IsPlaneMemberSurface(const std::string& surface_id) const;

  // Stage 2A visual rule (SPATIAL_RUNTIME_MIGRATION.md §6b): a surface renders ACTIVE iff
  // (an active plane exists AND it is in that plane) OR (no active plane AND it is the active
  // free surface). Everything else is inactive (dimmed). This is the single predicate the
  // PlaneVisualProjector pushes to each surface.
  bool IsSurfaceVisuallyActive(const std::string& surface_id) const;

  // SPATIAL MIGRATION Stage 2A — fire on every activation TRANSITION (after state + log are
  // final), so the visual projector can re-project. Using a callback (not a second bus
  // subscription) guarantees ordering: the projector always reads settled state. Set null to
  // detach (the projector does this in its dtor).
  void SetOnChanged(std::function<void()> cb) { on_changed_ = std::move(cb); }

 private:
  void OnEvent(RuntimeEvent event, SurfaceShell* surface);
  // Map a surface id → its owning plane root ("" if the surface is in no plane).
  std::string PlaneRootOf(const std::string& surface_id) const;

  WorkspaceCompositionGraph* graph_;  // not owned (product) — plane membership
  EventBus* bus_;                     // not owned (runtime)
  EventBus::Token bus_token_ = 0;

  std::string active_root_;         // "" = no active plane (free surface is active)
  std::string leader_surface_id_;   // last activator within the active plane (or free id)
  std::function<void()> on_changed_;  // fired after each transition (Stage 2A projection hook)
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_PLANE_ACTIVATION_MODEL_H_
