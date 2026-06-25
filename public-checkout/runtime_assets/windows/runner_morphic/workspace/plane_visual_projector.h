#ifndef RUNNER_WORKSPACE_PLANE_VISUAL_PROJECTOR_H_
#define RUNNER_WORKSPACE_PLANE_VISUAL_PROJECTOR_H_

#include <functional>
#include <string>

#include "spatial_config.h"  // R5-lite — toggle source of truth

class SurfaceManager;

// SPATIAL MIGRATION — Stage 2A: Plane visual projection (content-level).
//
// Pushes per-surface "is your plane active?" state DOWN to each surface's Flutter content
// over a 'morphic/plane' MethodChannel (built from the surface's engine messenger), where
// PlaneDimmable renders dim + slight desaturation for inactive planes. This is the perceptual
// payoff of the migration — two surfaces in one plane stay bright together while unrelated
// planes recede — done entirely in CONTENT (no DWM material, no native projection), which is
// what keeps it substrate-independent.
//
// Trigger: PlaneActivationModel::SetOnChanged (a callback, NOT a second bus subscription — so
// it always reads settled activation state; bus handler order is otherwise unspecified).
//
// CRITICAL (SPATIAL_RUNTIME_MIGRATION.md §4b): VISUAL ONLY. It invokes a render-state method;
// it never routes input, capture, or foreground. A dimmed surface stays fully interactive.
//
// Firewall: product layer. Uses PlaneActivationModel (product) + SurfaceManager / SurfaceShell
// public accessors (FindById/Surfaces/messenger). No runtime mutation; runtime stays
// plane-agnostic.
namespace morphic::workspace {

class PlaneActivationModel;

class PlaneVisualProjector {
 public:
  // Compile-time kill switch (default true — Stage 1 is proven and this is the stage under
  // test; flip false for instant rollback to invisible Stage 1).
  static constexpr bool kEnablePlaneVisualProjection =
      morphic::config::kEnablePlaneVisualProjection;  // value: spatial_config.h

  // `material_of` answers "what is this surface's material identity?" as a content-layer token
  // ("standard" | "mica" | "acrylic" | "tabbed"), wired from SurfaceEcology::GetSurfaceMaterial.
  // Pushed to each surface's content so it renders the matching Morphic MaterialRecipe (or stays
  // opaque for "standard"). Opaque callback — keeps the projector decoupled from ecology/policy
  // types (firewall-clean).
  PlaneVisualProjector(PlaneActivationModel* model, SurfaceManager* manager,
                       std::function<std::string(const std::string&)> material_of);
  ~PlaneVisualProjector();

  PlaneVisualProjector(const PlaneVisualProjector&) = delete;
  PlaneVisualProjector& operator=(const PlaneVisualProjector&) = delete;

 private:
  // Push current visual-active + glass state to every live surface.
  void ProjectAll();

  PlaneActivationModel* model_;  // not owned (product) — activation truth + the change hook
  SurfaceManager* manager_;      // not owned (runtime) — enumerate surfaces + messengers
  std::function<std::string(const std::string&)> material_of_;  // surface id → material token
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_PLANE_VISUAL_PROJECTOR_H_
