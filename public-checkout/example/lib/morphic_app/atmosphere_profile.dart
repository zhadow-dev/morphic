import 'package:flutter/material.dart';

import '../plane_dimmable.dart';
import 'morphic_spatial_theme.dart';

/// M2.5 — AtmosphereProfile: a surface's authored PRESENTATION, bundled in ONE named place.
///
/// The per-role "artistic guesses" (which glass mode, which backdrop, which field, which depth) were
/// scattered across each surface + the spawn spec. This collects them so they live centrally — the one
/// place to tune the scene — instead of becoming chaos as surfaces multiply.
///
/// CRITICAL (the line that keeps this honest): **presentation ONLY.** It carries no geometry, position,
/// size, layout, or HWND truth, and it NEVER repositions/scales/negotiates anything. It is NOT a theme
/// engine / style framework / composition solver — just bundled tokens + a thin apply widget. If it ever
/// grows logic that moves or sizes a window, that's the drift back into a "spatial engine" — stop.
class AtmosphereProfile {
  final MorphicArchetype archetype;
  final SurfaceDepth depth; // presence (constant role weight) — pairs with PlaneDimmable
  final String transparency; // spawn hint (SurfaceSpec): 'opaque' | 'transparent_content' | 'full_glass'
  final String backdrop; // spawn hint (SurfaceSpec): 'none' | 'mica' | 'acrylic' | 'tabbed'
  final BoxDecoration field; // content: the painted field (a flat color OR the anchor atmosphere gradient)
  // M2.6 — VITALITY: graded activation RESPONSE (orthogonal to depth's constant PRESENCE). How much this
  // surface "awakens" / catches light when its plane is active. Anchor 1.0 radiates; companions catch
  // LESS (NOT equal) → a dominant surface radiates contextual vitality, it does NOT synchronize the plane.
  final double vitality;

  const AtmosphereProfile({
    required this.archetype,
    required this.depth,
    required this.transparency,
    required this.backdrop,
    required this.field,
    required this.vitality,
  });

  /// The canonical Notes scene's authored atmospheres — tune the scene HERE, not in five files.
  static const editor = AtmosphereProfile(
    archetype: MorphicArchetype.anchor,
    depth: SurfaceDepth.primary,
    transparency: 'full_glass',
    backdrop: 'acrylic',
    field: BoxDecoration(gradient: MorphicGlass.anchorAtmosphere), // environmental field (owns the field)
    vitality: 1.0, // the anchor radiates — full activation light
  );
  static const inspector = AtmosphereProfile(
    archetype: MorphicArchetype.companion,
    depth: SurfaceDepth.contextual,
    transparency: 'full_glass',
    backdrop: 'mica',
    field: BoxDecoration(color: MorphicGlass.companionField),
    vitality: 0.4, // companion: catches ~40% of the anchor's radiance — awakens, doesn't equal
  );
  static const utility = AtmosphereProfile(
    archetype: MorphicArchetype.utility,
    depth: SurfaceDepth.contextual,
    transparency: 'full_glass',
    backdrop: 'tabbed',
    field: BoxDecoration(color: MorphicGlass.utilityField),
    vitality: 0.18, // utility: faintest — a quiet satellite catching a little light
  );

  // M2.8 — THE HABITAT (the spatial-native showcase). A continuous-presence scene, NOT a productivity
  // app: a presence FIELD (anchor) + two orbital companions. Same presentation grammar, ambient ontology.
  static const presence = AtmosphereProfile(
    archetype: MorphicArchetype.anchor,
    depth: SurfaceDepth.primary,
    transparency: 'full_glass',
    backdrop: 'acrylic',
    field: BoxDecoration(gradient: MorphicGlass.presenceAtmosphere), // the deep presence field
    vitality: 1.0, // the presence radiates
  );
  static const echo = AtmosphereProfile(
    archetype: MorphicArchetype.companion,
    depth: SurfaceDepth.contextual,
    transparency: 'full_glass',
    backdrop: 'mica',
    field: BoxDecoration(color: MorphicGlass.companionField),
    vitality: 0.3, // memory echoes: faint, peripheral, half-present
  );
  static const ambient = AtmosphereProfile(
    archetype: MorphicArchetype.companion,
    depth: SurfaceDepth.contextual,
    transparency: 'full_glass',
    backdrop: 'mica',
    field: BoxDecoration(color: MorphicGlass.companionField),
    vitality: 0.5, // ambient context (the live "now"): quietly awake
  );
}

/// Applies a surface's [AtmosphereProfile] to its content: the field decoration + the depth presence
/// (via [PlaneDimmable]). The surface's own Scaffold should be transparent so the field shows.
///
/// Thin by design — it just composes two existing primitives from the profile. No engine, no geometry.
/// Wrap a surface's root with it: `MorphicAtmosphere(profile: AtmosphereProfile.inspector, child: ...)`.
class MorphicAtmosphere extends StatelessWidget {
  final AtmosphereProfile profile;
  final Widget child;
  const MorphicAtmosphere({super.key, required this.profile, required this.child});

  @override
  Widget build(BuildContext context) => PlaneDimmable(
        depth: profile.depth,
        vitality: profile.vitality,
        child: DecoratedBox(decoration: profile.field, child: child),
      );
}
