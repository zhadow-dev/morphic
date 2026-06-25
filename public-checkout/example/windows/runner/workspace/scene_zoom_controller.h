#ifndef RUNNER_WORKSPACE_SCENE_ZOOM_CONTROLLER_H_
#define RUNNER_WORKSPACE_SCENE_ZOOM_CONTROLLER_H_

#include <windows.h>

#include <string>
#include <unordered_map>

class SurfaceShell;
class SurfaceModel;
class SurfaceManager;
class PresentationCoordinator;

// M2.8.3 — SCENE ZOOM (PRODUCT LAYER ONLY). The spatial-scene answer to "maximize".
//
// A composition is NOT a window that fills the monitor; it is an AUTHORED relationship the user can
// move CLOSER (immersive) or back (authored). This reprojects an entire composition-plane's member
// rects proportionally — a CAMERA MOVE, not a window state — so the anchor AND its companions scale
// together and the authored offsets/rhythm survive ("the monitor moved closer", never "a window
// maximized with corner popups").
//
// HARD LIMITS (this is the line that keeps it from being a window manager — do NOT cross):
//   * ONE-SHOT. Computes new bounds once per call and applies them. It NEVER reactively repositions,
//     auto-balances, resolves overlaps, or enforces anything continuously.
//   * From the AUTHORED SNAPSHOT, never live geometry. The snapshot is captured ONCE (the first zoom
//     from the authored state) and every zoom reprojects FROM it — so drift can never accumulate.
//   * Surfaces stay SOVEREIGN: independent HWNDs, still draggable/focusable/closable. The scene is a
//     SUGGESTED relationship, not ownership — the user may break it; zoom just re-suggests it.
//   * NO solver / constraints / docking / snapping / adaptive layout / content scaling. Dumb but
//     authored — like zooming a canvas, not a smart WM.
//
// FIREWALL: the runtime never learns "scene"/"zoom". morphic_runtime forwards an opaque {surfaceId,
// zoom} command; this product-layer controller reads the composition graph (membership) + SurfaceModel
// (public geometry get/set) + SurfaceManager (id -> shell). No runtime mutation, exactly like gravity.
namespace morphic::workspace {

class WorkspaceCompositionGraph;

// ===================== SCENE ZOOM CONTRACT (PERMANENT DOCTRINE — M2.8.3 Phase A) =====================
//  - Scene zoom NEVER becomes canonical geometry   (the authored snapshot is the ONLY truth).
//  - Scene zoom NEVER runs continuously             (one-shot per call; no loop, no solver of its own).
//      The smooth TRANSITION rides the EXISTING presentation settle seam (PresentationCoordinator: a
//      bounded, self-terminating, critically-damped ease it already owns) — scene zoom hands it a target
//      ONCE and never owns a tick/loop itself; the eased PRESENTED bounds stay throwaway (snapshot canon).
//  - Scene zoom NEVER owns layout                   (it SUGGESTS a relationship; it never enforces one).
//  - Scene zoom NEVER scales Flutter content        (it moves/sizes WINDOWS; content-fill is per-surface).
//  - Scene zoom NEVER prevents drag                 (surfaces stay sovereign; the user may break the scene).
//  - Scene zoom NEVER introduces constraints        (no docking / snapping / balancing / overlap-resolve).
//  - Scene zoom NEVER persists transformed bounds   (presented bounds are throwaway; the snapshot is canon).
//  - Authored geometry remains the ONLY truth; scene zoom is a temporary CAMERA reinterpretation.
// If a future change violates ANY line above, it has become a window manager — stop and revert.
// ====================================================================================================
class SceneZoomController {
 public:
  SceneZoomController(WorkspaceCompositionGraph* graph, SurfaceModel* model, SurfaceManager* manager,
                      PresentationCoordinator* coordinator);

  // Kill switch (M2.8.3 Phase B): false = instant reprojection (hard snap, pre-animation); true = ease
  // the PRESENTED projection to the target via the existing settle seam (bounded, self-terminating).
  static constexpr bool kAnimated = true;

  // Reproject the composition the surface `any_id` belongs to (root OR member) toward `zoom`:
  //   1.0 = authored scale; larger = closer. Clamped so the composition never overflows the work area
  //   (so a large value resolves to "fill the field" = immersive). Captures the authored snapshot on
  //   first use; NO-OP (no caching) until the plane actually has members to compose.
  void ZoomScene(const std::string& any_id, double zoom);

  // Return the composition to its authored snapshot (zoom -> 1.0) and forget it.
  void ResetScene(const std::string& any_id);

 private:
  std::string RootOf(const std::string& any_id) const;
  RECT WorkAreaFor(const RECT& bbox) const;
  // Apply ONE member's target bounds: eased via the settle seam (kAnimated) or instant SetBounds.
  void ApplyMemberBounds(SurfaceShell* surface, const RECT& target);

  WorkspaceCompositionGraph* graph_;  // product — plane membership (not owned)
  SurfaceModel* model_;               // runtime — geometry get/set (not owned)
  SurfaceManager* manager_;           // runtime — id -> shell (not owned)
  PresentationCoordinator* coordinator_;  // runtime — eased native-projection settle seam (not owned)
  // root_id -> {member_id -> AUTHORED rect}. Captured once; every reprojection reads from here.
  std::unordered_map<std::string, std::unordered_map<std::string, RECT>> authored_;
  bool reprojecting_ = false;  // CONTRACT guard: scene zoom is one-shot — never recursive/continuous.
};

}  // namespace morphic::workspace

#endif  // RUNNER_WORKSPACE_SCENE_ZOOM_CONTROLLER_H_
