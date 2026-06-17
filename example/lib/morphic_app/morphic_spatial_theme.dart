import 'package:flutter/material.dart';

/// MORPHIC SPATIAL VISUAL LANGUAGE v1 — the centralized grammar for sovereign spatial surfaces.
///
/// Goal: surfaces read as ONE authored environment, not desktop windows. The language is SUBTRACTION +
/// restraint — dissolved chrome, generous breathing, low edge energy, atmospheric (not flat) fields. NO
/// neon, NO heavy glassmorphism, NO glow spam. Improvised per-surface styling is the enemy of coherence;
/// everything visual references these tokens.
///
/// SCOPE: this is TOKENS (values) only — NOT a layout/composition/theming engine. Composition, placement,
/// and the immersive-maximize behavior live in the (frozen) runtime + product layer, never here.

/// Breathing scale — environmental, generous. One [margin] unifies a surface's content column so the
/// surface reads as a continuous field rather than stacked panels.
class MorphicSpace {
  const MorphicSpace._();
  static const double margin = 24; // the canvas breathing margin (title / body / status align to it)
  static const double gap = 14;
  static const double tight = 8;
}

class MorphicRadius {
  const MorphicRadius._();
  static const double surface = 14; // inner content cards (the WINDOW corner is rounded by DWM)
  static const double control = 6;
}

/// Atmospheric field — dark + low-energy, but NEVER pure black (OLED void reads cheap). Two field tiers:
/// the [canvas] for the dominant anchor, a slightly recessed [canvasDeep] for quieter companions.
class MorphicColors {
  const MorphicColors._();
  static const Color canvas = Color(0xFF14181E); // anchor field
  static const Color canvasDeep = Color(0xFF11151B); // companion / utility field (recessed)
  static const Color ink = Color(0xFFE6EDF3); // primary text — highest clarity
  static const Color body = Color(0xFFC9D4E0); // body text
  static const Color muted = Color(0xFF6E7A8A); // labels / chrome
  static const Color whisper = Color(0xFF49525C); // quietest — status, contextual labels
  static const Color accent = Color(0xFF58A6FF); // cursor / active marks — used SPARINGLY
}

/// Translucent FIELD tints painted OVER the native ACCENT blur (Sprint B). Intensity rises anchor →
/// utility: the anchor stays MOSTLY solid (it must own the field when maximized — a fully-glass editor
/// would bleed wallpaper and break the immersive model); companions/utilities get progressively more
/// glass so they "melt" into the editor canvas behind them. Requires the surface spawned `full_glass`
/// + a `backdrop` (see NotesApp). Pure alpha — tune to taste. Windows "transparency effects" must be ON
/// for the blur (else it falls back to a solid tint, which still reads fine).
class MorphicGlass {
  const MorphicGlass._();
  static const Color anchorField = Color(0xD114181E); // ~82% — deep smoked glass, readable, owns field
  static const Color companionField = Color(0x8011151B); // ~50% — translucency becomes visible
  static const Color utilityField = Color(0x5911151B); // ~35% — near-atmospheric, ghosted instrument

  /// PHASE 2A — the anchor's ENVIRONMENTAL field: a near-opaque atmospheric gradient (a faint upper
  /// exposure lift fading to a darker vignette at the edges). It OWNS the field so the wallpaper
  /// recedes (barely perceptible when maximized), and adds cinematic depth so the editor reads as an
  /// ENVIRONMENT, not a flat dark void. Differences are TINY on purpose — atmosphere, not a "gradient".
  static const Gradient anchorAtmosphere = RadialGradient(
    center: Alignment(-0.25, -0.55), // soft off-centre light source (film exposure, not a spotlight)
    radius: 1.5,
    // ~94–97% opaque so the wallpaper recedes; the gradient is in LUMINANCE (lift centre → dark
    // vignette edges), not opacity — that's the atmosphere.
    colors: [Color(0xF01B202B), Color(0xF4141821), Color(0xF80D1017)],
    stops: [0.0, 0.5, 1.0],
  );

  /// M2.7 — SCENE CONTEXT DEPTH: an authored depth veil for the editor's CONTEXT FIELD when maximized
  /// (the open right region where the companion cluster floats). Layered over the anchor atmosphere in
  /// the SCENE state ONLY, it deepens toward the lower-right (with the field's upper-left light) so the
  /// large field reads as receding atmospheric DEPTH the companions float in — NOT a flat dead void.
  /// Luminance only (a faint cool-black), kept subtle on purpose; tune the trailing alpha to taste.
  static const Gradient sceneContextDepth = LinearGradient(
    begin: Alignment.topLeft,
    end: Alignment.bottomRight,
    colors: [Color(0x00060A12), Color(0x00060A12), Color(0x1A060A12)],
    stops: [0.0, 0.5, 1.0],
  );

  /// M2.8 — THE HABITAT presence atmosphere: a deep, calm midnight field with a faint cool central
  /// lift (where the breathing core lives) falling to a near-black vignette. Distinct from the
  /// editor's anchorAtmosphere — this is a PRESENCE field, not a work canvas: quieter, deeper, more
  /// nocturnal. Luminance gradient (not opacity), ~95–98% opaque so it OWNS the field. Tune the lift.
  static const Gradient presenceAtmosphere = RadialGradient(
    center: Alignment(-0.12, -0.08), // soft off-centre core glow seat
    radius: 1.3,
    colors: [Color(0xF21A2233), Color(0xF6121829), Color(0xFA0A0E18)],
    stops: [0.0, 0.55, 1.0],
  );
}

/// Typography hierarchy — a small, deliberate set (not a generative type scale).
class MorphicType {
  const MorphicType._();
  static const TextStyle title =
      TextStyle(color: MorphicColors.ink, fontSize: 14, fontWeight: FontWeight.w600);
  static const TextStyle body = TextStyle(
      color: MorphicColors.body, fontSize: 13, height: 1.5, fontFamily: 'Consolas');
  static const TextStyle label = TextStyle(
      color: MorphicColors.muted, fontSize: 10, fontWeight: FontWeight.w700, letterSpacing: 1.4);
  static const TextStyle whisper = TextStyle(color: MorphicColors.whisper, fontSize: 10);
}

/// SURFACE ARCHETYPES — visual identity per spatial ROLE. Orthogonal to behavioral `SurfaceKind`, and
/// it pairs with `SurfaceDepth` (anchor → primary presence; companion/utility → contextual/quieter).
///
///  - anchor    : the dominant work surface (editor/workspace). Owns the atmosphere — deepest field,
///                lowest chrome, widest breathing, strongest content clarity.
///  - companion : semantic context (inspector). Quieter — recessed field, softer labels, less edge.
///  - utility   : transient tools (palette/command). Lightweight, floating, small footprint.
enum MorphicArchetype { anchor, companion, utility }

class MorphicArchetypeStyle {
  final Color field; // surface background
  final EdgeInsets pad; // breathing margins
  final double chrome; // 0..1 chrome visibility (lower = more dissolved); for the surface to honor
  const MorphicArchetypeStyle({required this.field, required this.pad, required this.chrome});
}

const Map<MorphicArchetype, MorphicArchetypeStyle> kMorphicArchetypes = {
  MorphicArchetype.anchor: MorphicArchetypeStyle(
    field: MorphicColors.canvas,
    pad: EdgeInsets.fromLTRB(24, 16, 16, 12),
    chrome: 0.25,
  ),
  MorphicArchetype.companion: MorphicArchetypeStyle(
    field: MorphicColors.canvasDeep,
    pad: EdgeInsets.fromLTRB(18, 14, 14, 12),
    chrome: 0.16,
  ),
  MorphicArchetype.utility: MorphicArchetypeStyle(
    field: MorphicColors.canvasDeep,
    pad: EdgeInsets.fromLTRB(14, 12, 12, 10),
    chrome: 0.14,
  ),
};
