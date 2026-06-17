#ifndef RUNNER_SURFACE_POLICY_SURFACE_ECOLOGY_H_
#define RUNNER_SURFACE_POLICY_SURFACE_ECOLOGY_H_

#include <flutter/dart_project.h>

#include <optional>
#include <string>
#include <unordered_map>

#include "spatial_config.h"  // R5-lite — toggle source of truth
#include "surface_policy/composition_types.h"
#include "surface_policy/surface_appearance.h"
#include "surface_policy/surface_kind.h"
#include "surface_policy/surface_policy.h"
#include "surface_policy/surface_registry.h"
#include "workspace/workspace_composition_graph.h"
#include "workspace/workspace_manager.h"

class SurfaceManager;

// PHASE 10.1 — SurfaceEcology.
//
// The product-layer facade that ties policy + registry + workspace together and is
// the ONE place the product layer touches the runtime. It owns the ecology trio
// (registry, workspace manager; policy is stateless/static) and a non-owning
// SurfaceManager* (the runtime lifecycle entry).
//
// SpawnSurface is the integration point: it resolves policy → builds a descriptor →
// registers it → adds it to a workspace → calls SurfaceManager::CreateSurface (the
// UNCHANGED runtime entry). The dependency arrow points DOWN: ecology → runtime.
// The runtime never learns SurfaceKind. If a spawn is rejected by policy
// (CanSpawn == false), no runtime surface is created.
namespace morphic::policy {

// SPATIAL MIGRATION F1 — compile-time toggle for plane-shadow reconciliation (member
// drop-shadows suppressed inside a plane; root casts the shared-plane shadow). Default true;
// flip false to A/B the perceptual before/after (does suppressing member shadows actually make
// the plane read as one object?) and for instant rollback.
inline constexpr bool kEnablePlaneShadowReconcile =
    morphic::config::kEnablePlaneShadowReconcile;  // value: spatial_config.h

// PHASE 11/12 — optional per-surface spawn overrides (e.g. from the Dart ecology
// channel). Any unset field falls back to the kind's policy default. NOTE:
// composition_mode records the spatial-mode / shared-drag ATTRIBUTE — the actual
// shared-drag MOVEMENT is the deferred Phase 12D, so setting Composed today only records
// intent (and, once a composition plane exists, affects shadow); it does NOT yet move
// surfaces together.
struct SpawnOverrides {
  std::optional<SurfaceCompositionMode> composition_mode;
  std::optional<SurfaceCornerStyle> corners;
  std::optional<bool> shadow;  // true = cast a drop shadow; false = none
  std::optional<SurfaceBackdrop> backdrop;
  std::optional<SurfaceTransparencyMode> transparency_mode;  // PHASE 11E
  // SPATIAL CHROME — per-surface shell chrome. `chromeless` removes the native
  // title strip + resize borders entirely (full-bleed Flutter child; no grab
  // zones — the surface is positioned programmatically). `corner_radius_px` is
  // the arbitrary window corner radius: STORED here and delivered to the
  // surface's engine, which shapes the window via content alpha (antialiased,
  // 0 → circle); true shape requires backdrop None — see
  // SurfaceAppearance::corner_radius_px.
  std::optional<bool> chromeless;
  std::optional<int> corner_radius_px;
  // MORPHIC NG Phase 3 -- spatial material spec (compositor-level, only
  // meaningful for backend=spatial): shape
  // ("rounded"|"capsule"|"hexagon"|"circle"),
  // material ("none"|"acrylic"), tint (ARGB over the blur), elevation
  // (compositor shadow depth, px).
  std::optional<std::string> shape;
  std::optional<std::string> material;
  std::optional<unsigned int> material_tint_argb;
  std::optional<int> elevation_px;
  // MORPHIC NG Stage 2/5 -- which substrate realizes the surface.
  //   "native"  (default) -- an HWND, exactly as today.
  //   "spatial" -- the NG compositor adopts the surface: its HWND is parked
  //                offscreen as an engine host and its pixels are captured and
  //                composited as a shaped GPU visual (see compositor_ng/).
  std::optional<std::string> backend;
};

class SurfaceEcology {
 public:
  SurfaceEcology(SurfaceManager* manager, EventBus* bus);
  ~SurfaceEcology();  // PHASE 10.3E — unsubscribe the activation observer

  SurfaceEcology(const SurfaceEcology&) = delete;
  SurfaceEcology& operator=(const SurfaceEcology&) = delete;

  SurfaceRegistry& registry() { return registry_; }
  workspace::WorkspaceManager& workspaces() { return workspaces_; }
  // PHASE 12B — composition planes (shared-drag membership; projection-only).
  workspace::WorkspaceCompositionGraph& composition_graph() {
    return composition_graph_;
  }

  // Spawn a product surface. `entrypoint` is the Dart entry-point name (the
  // runtime detail the product chooses per surface). `workspace_id` empty → the
  // current workspace. `parent_id` is the owning surface (for kinds that follow a
  // parent). Returns the new surface id, or nullopt if policy rejected the spawn or
  // the runtime failed to create it.
  std::optional<std::string> SpawnSurface(
      const flutter::DartProject& base_project, SurfaceKind kind,
      const std::string& id, const std::string& entrypoint, int x, int y,
      int width, int height, const std::string& workspace_id = "",
      std::optional<std::string> parent_id = std::nullopt,
      const SpawnOverrides& overrides = {});

  // PHASE 10.2 — destroy a surface by id. If the surface owns children
  // (FollowsParent points to it), cascade-destroy them first (ownership
  // semantics). Unregisters from registry + workspace, then destroys via
  // SurfaceManager.
  bool DestroySurface(const std::string& id);

  // PHASE 10.2 — destroy a workspace and all its surfaces (cascade).
  bool DestroyWorkspace(const std::string& workspace_id);

  // PHASE 10.2 — structured ecology summary for the Dart-side UI.
  // Returns surface count, workspace count, per-surface descriptors.
  struct EcologySummary {
    struct SurfaceInfo {
      std::string id;
      std::string kind;
      std::string workspace_id;
      std::string parent;
      bool detachable;
      bool groupable;
      bool focusable;
      bool persistent;
      bool follows_parent;
    };
    struct WorkspaceInfo {
      std::string id;
      std::string title;
      std::vector<std::string> surface_ids;
    };
    std::vector<SurfaceInfo> surfaces;
    std::vector<WorkspaceInfo> workspaces;
    std::string current_workspace;
  };
  EcologySummary GetSummary() const;

  // Log a one-line ecology summary (boot diagnostic): surface count + kinds +
  // workspaces. Proves the product layer is wired without changing behavior.
  void LogSummary() const;

  // PHASE 11B — re-resolve + re-project a surface's native appearance (DWM shadow /
  // corners / dark mode). Appearance is runtime-mutable (Q4); call after a descriptor's
  // composition_mode or plane membership changes. No-op if the surface is unknown.
  void ReapplyAppearance(const std::string& id);

  // SPATIAL MIGRATION F1 — plane-shadow reconciliation. When a surface's plane membership
  // changes, re-project ONLY its shadow per plane policy (root → SharedPlane, member → None)
  // from the remembered applied appearance — preserving all other per-surface overrides. Fixes
  // the spawn→AddMember sequencing gap where member-shadow suppression never fired. No-op for
  // floating surfaces or surfaces not spawned through here. VISUAL ONLY.
  void ReconcilePlaneShadow(const std::string& id);

  // SPATIAL MIGRATION (plane material coherence) — a plane MEMBER adopts its root's glass
  // material (transparency_mode + backdrop) so it joins the same visual field instead of
  // reading as an opaque foreign panel. Native half (content half is the 'morphic/plane_material'
  // push). Keeps the member's own corners/shadow. Gated by kEnablePlaneMaterialCoherence;
  // no-op for roots / floating / unknown surfaces. VISUAL ONLY.
  void ReconcilePlaneMaterial(const std::string& id);

  // SPATIAL CHROME — the surface's arbitrary corner radius (px; 0 = none). A
  // CONTENT-delivered property: the surface's engine shapes the window via
  // content alpha (see SurfaceAppearance::corner_radius_px for why a native
  // region/DWM projection is impossible). Set updates the stored truth; the
  // runtime channel layer pushes the change to the surface's engine.
  int GetCornerRadius(const std::string& id) const;
  bool SetCornerRadius(const std::string& id, int radius_px);

  // MORPHIC NG Stage 2 -- the surface's declared backend ("native"/"spatial").
  std::string GetSurfaceBackend(const std::string& id) const;

  // MORPHIC NG Phase 3 -- the spatial material spec for backend=spatial
  // surfaces (compositor shapes/materials/shadows; defaults when unset).
  struct SpatialMaterialSpec {
    std::string shape = "rounded";   // rounded | capsule | hexagon | circle
    std::string material = "none";   // none | acrylic
    unsigned int tint_argb = 0;      // 0 = material default tint
    int elevation_px = 0;            // 0 = no compositor shadow
  };
  SpatialMaterialSpec GetSpatialMaterial(const std::string& id) const;

  // SPATIAL MIGRATION — true iff the surface's CURRENT applied appearance is a glass mode
  // (FullGlass/TransparentContent). Read-only.
  bool IsSurfaceGlass(const std::string& id) const;

  // SPATIAL MIGRATION (Stage M1) — the surface's MATERIAL IDENTITY as a content-layer token:
  // "standard" (opaque/grounded), or a glass material "mica"/"acrylic"/"tabbed". The
  // PlaneVisualProjector pushes this to each surface's content, which renders the matching
  // Morphic MaterialRecipe (tint/opacity/frost) over the native blur. Read-only.
  std::string GetSurfaceMaterial(const std::string& id) const;

 private:
  // PHASE 10.3E — on SurfaceActivated, compute + LOG the activation cluster
  // (workspace root + owned utilities). Observe-only this pass: Win32 owner chains
  // already raise owned windows with their owner, so we don't mutate runtime
  // activation here — we surface the semantic truth for verification/future use.
  void OnActivation(SurfaceShell* surface);

  SurfaceManager* manager_;  // not owned (runtime-owned)
  EventBus* bus_;            // not owned (runtime-owned)
  int activation_token_ = 0;
  SurfaceRegistry registry_;
  workspace::WorkspaceManager workspaces_;
  workspace::WorkspaceCompositionGraph composition_graph_;  // PHASE 12B — composition planes
  // SPATIAL MIGRATION F1 — the appearance actually applied per surface (resolved + overrides),
  // so plane-shadow reconciliation can adjust ONLY the shadow without re-resolving (which would
  // drop per-surface overrides). Keyed by surface id; erased on destroy.
  std::unordered_map<std::string, SurfaceAppearance> applied_appearance_;
  // MORPHIC NG Stage 2 -- declared backend per surface id (absent = "native").
  std::unordered_map<std::string, std::string> backend_;
  // MORPHIC NG Phase 3 -- spatial material specs (backend=spatial only).
  std::unordered_map<std::string, SpatialMaterialSpec> spatial_material_;
};

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_SURFACE_ECOLOGY_H_
