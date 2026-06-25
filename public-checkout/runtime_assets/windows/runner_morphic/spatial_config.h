#ifndef RUNNER_SPATIAL_CONFIG_H_
#define RUNNER_SPATIAL_CONFIG_H_

// SPATIAL MIGRATION — R5-lite: single source of CONTROL for the spatial-era experiment toggles.
//
// These were scattered across plane / material / policy headers. Their VALUES now live here, so
// flipping an experiment is a one-file edit. The original sites keep their symbol names but
// alias these, so every `if constexpr (...)` use-site is unchanged. **Edit toggles HERE.**
//
// Compile-time only — runtime-mutable config / diagnostics panels / startup flags are a DEFERRED
// productization step (do not build until the host/SDK split is earned; see RUNTIME_CARTOGRAPHY.md).
// Flip a value, rebuild.
//
// NOT here (live elsewhere by nature, noted so the map is complete):
//   - kEnableActivePlaneLighting / kActiveLift* — DART (example/lib/plane_dimmable.dart);
//     content-level lighting, cannot be a C++ constant.
//   - Older pre-spatial runtime toggles (kCompositorEnabled, kPresentationEnabled,
//     kEnableNativeAppearance, kEnableCompositionMovement, kQualificationProbe) — left in place;
//     may be folded in later.
namespace morphic::config {

// Stage 1 — plane activation derived + logged (observe-and-derive); inert with no planes.
inline constexpr bool kEnablePlaneActivation = true;

// Stage 2A — content-level plane visual coherence (active/dim + glass material push).
inline constexpr bool kEnablePlaneVisualProjection = true;

// F1 — plane-shadow reconciliation (member shadow suppressed; root casts the shared-plane shadow).
inline constexpr bool kEnablePlaneShadowReconcile = true;

// Plane material coherence — a member adopts its plane root's glass material (native + content).
inline constexpr bool kEnablePlaneMaterialCoherence = true;

// Geometric docking (8D). DISABLED by LAW (§4c: geometry may never mutate topology). Two stacked
// surfaces must NOT auto-group/co-move. Set true ONLY to revive the 8D semantic-docking experiment.
inline constexpr bool kEnableGeometricDocking = false;

// M2.3G — Relationship Gravity. When a composition-plane ROOT maximizes, its composed companions
// softly reposition into top-right contextual slots (then stay INDEPENDENT — initial reposition,
// NOT continuous docking/snapping). Restores on un-maximize. Toggle for direct A/B perception tuning.
inline constexpr bool kEnableRelationshipGravity = true;

}  // namespace morphic::config

#endif  // RUNNER_SPATIAL_CONFIG_H_
