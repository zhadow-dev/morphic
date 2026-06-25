import 'package:flutter/widgets.dart';

/// SPATIAL MIGRATION — Stage M1: Morphic-defined material identity.
///
/// Mica / Acrylic / Tabbed look near-identical from the OS because the native ACCENT substrate
/// is just "blur + tint" — same blur radius, same path. Identity is NOT an OS API; it's a
/// MORPHIC visual recipe rendered in CONTENT, OVER the native blur.
///
/// HONEST CONSTRAINT: Flutter content composites ON TOP of the DWM blur — it can ADD (tint,
/// frost, edge, opacity) but cannot SUBTRACT (it can't desaturate/contrast the blur behind it).
/// So a recipe differentiates via ADDITIVE treatment. That's enough to make the three materials
/// feel distinct; deeper knobs (edge bloom, noise, content dimming) are follow-on fields.
///
/// LAYERING (now correct): Windows blur = substrate · Morphic material = identity · plane = orchestration.
class MaterialRecipe {
  /// Additive tint laid over the blur (the dominant identity cue).
  final Color tint;
  final double tintOpacity;

  /// A light "frost" wash that calms/brightens and lowers contrast (Mica-like). 0 = none.
  final Color frost;
  final double frostOpacity;

  /// A 1px top edge-light (light catches the top of a glass panel) — adds UI-forward edge
  /// definition. 0 = none (a soft, edgeless material). First taste of environmental lighting.
  final double edgeHighlight;

  const MaterialRecipe({
    required this.tint,
    required this.tintOpacity,
    this.frost = const Color(0xFFFFFFFF),
    this.frostOpacity = 0.0,
    this.edgeHighlight = 0.0,
  });

  /// `standard` = no material layer (opaque/grounded surface; the default).
  static const MaterialRecipe? standard = null;

  /// Mica — grounded / calm / wallpaper-coupled: low tint so the wallpaper reads through,
  /// warm-neutral, a touch of frost for a soft matte feel.
  static const mica = MaterialRecipe(
    tint: Color(0xFF3A4150),
    tintOpacity: 0.20,
    frost: Color(0xFFCFD8E3),
    frostOpacity: 0.05,
  );

  /// Acrylic — elevated / vibrant / floating: cooler + DENSER tint (wallpaper recedes more than
  /// Mica → more UI-forward contrast), plus an edge-light so it reads crisper and more "glassy"
  /// — the clear step up in elevation from Mica.
  static const acrylic = MaterialRecipe(
    tint: Color(0xFF12303F),
    tintOpacity: 0.42,
    edgeHighlight: 0.07,
  );

  /// Tabbed — dense / productivity / flatter: a dark, MORE-opaque neutral so the wallpaper
  /// recedes hard and the surface reads as semi-opaque productivity glass (reduced atmospheric
  /// depth), not heavy atmosphere. No edge-light → flat/app-like.
  static const tabbed = MaterialRecipe(
    tint: Color(0xFF15181E),
    tintOpacity: 0.60,
  );

  /// Map a material token (from C++ `GetSurfaceMaterial`) to its recipe. `standard` (and any
  /// unknown token) → null = opaque, no material layer.
  static MaterialRecipe? forToken(String token) {
    switch (token) {
      case 'mica':
        return mica;
      case 'acrylic':
        return acrylic;
      case 'tabbed':
        return tabbed;
      case 'glass': // glass with no named material → treat as a neutral acrylic
        return acrylic;
      default: // 'standard' / unknown
        return null;
    }
  }
}

/// Renders a recipe as full-bleed additive layers OVER the native blur, BEHIND the surface UI.
/// Non-interactive (IgnorePointer) and inert when [recipe] is null (Standard).
class MaterialLayer extends StatelessWidget {
  final MaterialRecipe? recipe;
  const MaterialLayer(this.recipe, {super.key});

  @override
  Widget build(BuildContext context) {
    final r = recipe;
    if (r == null) return const SizedBox.shrink();
    return IgnorePointer(
      child: Stack(
        fit: StackFit.expand,
        children: [
          ColoredBox(color: r.tint.withValues(alpha: r.tintOpacity)),
          if (r.frostOpacity > 0)
            ColoredBox(color: r.frost.withValues(alpha: r.frostOpacity)),
          // Edge-light — a hairline highlight along the top edge (light from above).
          if (r.edgeHighlight > 0)
            Align(
              alignment: Alignment.topCenter,
              child: Container(
                height: 1.0,
                color: const Color(0xFFFFFFFF).withValues(alpha: r.edgeHighlight),
              ),
            ),
        ],
      ),
    );
  }
}
