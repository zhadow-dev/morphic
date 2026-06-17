#ifndef RUNNER_SURFACE_POLICY_SURFACE_APPEARANCE_H_
#define RUNNER_SURFACE_POLICY_SURFACE_APPEARANCE_H_

#include "spatial_config.h"  // R5-lite — toggle source of truth

// PHASE 11A/11E — SurfaceAppearance.
//
// PURE appearance metadata (product layer). NO Win32 here — the mapping to concrete DWM
// attributes lives in native_appearance_projection.{h,cpp}. The runtime never sees this
// struct or SurfaceKind. Appearance is STRICTLY non-semantic: toggling any of it changes
// NOTHING behavioral (activation, z, interaction, topology, geometry).
namespace morphic::policy {

// SPATIAL MIGRATION (plane material coherence) — compile-time toggle. When true, a plane
// MEMBER adopts the plane ROOT's glass material (transparency_mode + backdrop) so it reads as
// part of the same visual field instead of an opaque foreign panel, and the member's content
// is told to render translucent (frosted) so the glass shows. Default true; flip false to A/B
// the before/after and for instant rollback. Drives BOTH the native reconcile
// (SurfaceEcology::ReconcilePlaneMaterial) and the content push (PlaneVisualProjector →
// 'morphic/plane_material'), so they stay in lockstep (no translucent-over-opaque black).
inline constexpr bool kEnablePlaneMaterialCoherence =
    morphic::config::kEnablePlaneMaterialCoherence;  // value: spatial_config.h

// System backdrop intent. NOTE (verified): the DWM material only renders its live blur
// under conditions (transparency-effects on; Acrylic needs the FOREGROUND window; the
// frameless-shell eligibility is under investigation — 11E-B). Set regardless; falls back
// to a solid tint when conditions aren't met.
enum class SurfaceBackdrop { None, Mica, Acrylic, Tabbed };

enum class SurfaceCornerStyle { Default, Rounded, SmallRounded, Square };

// SPATIAL MIGRATION — surface VISUAL MODE (the role-level "grounded vs atmospheric" choice).
//   Standard — opaque, normal shadow, NO blur. A grounded, fast, stable window (VSCode /
//              Figma / Linear feel). THE DEFAULT — most surfaces should be grounded.
//   Glass    — translucent + DWM backdrop (FullGlass). A SELECTIVE semantic upgrade for
//              surfaces whose ROLE benefits from floating/atmosphere (overlays, opt-in
//              workspaces). Translucency is hierarchical, not universal — "everything glass"
//              is visual soup. Resolves to transparency_mode + backdrop in ResolveAppearance.
enum class SurfaceVisualMode { Standard, Glass };

inline const char* ToString(SurfaceVisualMode m) {
  switch (m) {
    case SurfaceVisualMode::Standard: return "standard";
    case SurfaceVisualMode::Glass:    return "glass";
  }
  return "?";
}

// Shadow PARTICIPATION (not a bool): Independent = own drop shadow; SharedPlane = the
// plane root casts for the whole plane (members suppressed → None by policy); None = flat.
enum class ShadowParticipation { None, SharedPlane, Independent };

// PHASE 11E — how much of the surface participates in DWM composition. Replaces the old
// coarse `transparent_content` bool with an explicit vocabulary.
//   Opaque            — no transparency; normal opaque surface (default).
//   TitlebarMaterial  — only the shell chrome (strip/borders) is glass; body opaque.
//                       [STUBBED — needs the 11E-B-confirmed material-aware shell paint.]
//   TransparentContent— the Flutter BODY may expose the backdrop (full-glass frame today).
//   FullGlass         — the entire surface participates (full-glass frame extension).
//   Hybrid            — glass shell + partially-opaque/translucent body.
//                       [STUBBED — needs the 11E-B-confirmed material-aware shell paint.]
// PROVEN today: Opaque and TransparentContent/FullGlass (alpha through the body works).
// STUBBED: TitlebarMaterial/Hybrid route their value but project as Opaque until the
// shell's per-region paint lands (gated behind 11E-B material qualification).
enum class SurfaceTransparencyMode {
  Opaque,
  TitlebarMaterial,
  TransparentContent,
  FullGlass,
  Hybrid,
};

struct SurfaceAppearance {
  ShadowParticipation shadow = ShadowParticipation::Independent;
  bool immersive_dark_mode = true;
  SurfaceBackdrop backdrop = SurfaceBackdrop::None;
  SurfaceCornerStyle corners = SurfaceCornerStyle::Default;
  SurfaceTransparencyMode transparency_mode = SurfaceTransparencyMode::Opaque;
  // SPATIAL CHROME — arbitrary corner radius in px (0 = none / use the DWM
  // preset above). NOT projected natively: a window region cannot clip the
  // DWM-composited output (verified — the ACCENT backdrop stays rectangular and
  // the glass degrades), and DWM offers only the fixed preset radii. Instead
  // the radius is DELIVERED to the surface's own engine ('surface.chrome'),
  // which shapes the window via content alpha over the full-glass frame —
  // antialiased, any radius (0 → circle). True shape therefore requires
  // backdrop None (an ACCENT blur backdrop is always rectangular: OS boundary).
  int corner_radius_px = 0;
};

// Does this mode extend the full-glass frame so the body can expose the backdrop?
// (The two PROVEN transparent modes today; TitlebarMaterial/Hybrid are stubbed → false.)
inline bool ModeWantsFullGlass(SurfaceTransparencyMode m) {
  return m == SurfaceTransparencyMode::TransparentContent ||
         m == SurfaceTransparencyMode::FullGlass;
}

inline const char* ToString(SurfaceTransparencyMode m) {
  switch (m) {
    case SurfaceTransparencyMode::Opaque:             return "opaque";
    case SurfaceTransparencyMode::TitlebarMaterial:   return "titlebar_material";
    case SurfaceTransparencyMode::TransparentContent: return "transparent_content";
    case SurfaceTransparencyMode::FullGlass:          return "full_glass";
    case SurfaceTransparencyMode::Hybrid:             return "hybrid";
  }
  return "?";
}

}  // namespace morphic::policy

#endif  // RUNNER_SURFACE_POLICY_SURFACE_APPEARANCE_H_
