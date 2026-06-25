#ifndef RUNNER_WORKSPACE_RELATIONSHIP_GRAVITY_CONTROLLER_H_
#define RUNNER_WORKSPACE_RELATIONSHIP_GRAVITY_CONTROLLER_H_

#include <windows.h>

#include <string>
#include <unordered_map>

#include "spatial_config.h"  // R5-lite — toggle source of truth

class SurfaceShell;
class SurfaceModel;
class SurfaceManager;

// M2.3G — RELATIONSHIP GRAVITY (PRODUCT LAYER ONLY).
//
// The dominant root surface IS the canvas: when it enters semantic maximize it fills the work area, so
// its composed companions float OVER it (never over wallpaper). To read as ONE authored environment
// (not scattered floating leftovers), companions RECOMPOSE into a right-side cluster on maximize — an
// "authored immersive composition", staged ONCE. Then they are fully independent again: drag away /
// focus / close / overlap / escape freely. NO resize, NO continuous enforcement, NO docking / snapping /
// constraints / z / topology / input. On un-maximize, moved companions return to their remembered rects.
// Cinematic staging, not a layout engine. Toggle off = exact-position preserve (A/B the two feels).
//
// FIREWALL: the runtime never learns "gravity" / "satellites" / "layout". It only fires a GENERIC
// window-state-changed hook (SurfaceModel::SetOnWindowStateChanged); morphic_runtime forwards that to
// this product-layer controller, which reads the composition graph (membership) + SurfaceModel (the
// public geometry get/set + maximize query) + SurfaceManager (id → shell). No runtime mutation; the
// runtime stays composition-agnostic, exactly like the plane projectors. Toggle: kEnableRelationshipGravity.
namespace morphic::workspace {

class WorkspaceCompositionGraph;

class RelationshipGravityController {
 public:
  static constexpr bool kEnabled = morphic::config::kEnableRelationshipGravity;

  RelationshipGravityController(WorkspaceCompositionGraph* graph, SurfaceModel* model,
                                SurfaceManager* manager);

  // Wired from the runtime's generic window-state hook. No-op unless `root` is a plane ROOT whose
  // maximize state changed since we last acted (we only apply once per maximize, restore once per
  // un-maximize). Minimize / native-restore that leave the maximize flag intact are ignored.
  void OnWindowState(SurfaceShell* root);

 private:
  void ApplyGravity(const std::string& root_id);    // remember + reposition companions
  void RestoreGravity(const std::string& root_id);  // return companions to remembered rects

  WorkspaceCompositionGraph* graph_;  // product — plane membership (not owned)
  SurfaceModel* model_;               // runtime — geometry get/set + maximize query (not owned)
  SurfaceManager* manager_;           // runtime — id → shell (not owned)
  // Roots we've applied gravity to → {member_id → pre-gravity rect}. Presence == "gravity applied".
  std::unordered_map<std::string, std::unordered_map<std::string, RECT>> remembered_;
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_RELATIONSHIP_GRAVITY_CONTROLLER_H_
