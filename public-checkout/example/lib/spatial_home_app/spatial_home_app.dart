import 'package:morphic/morphic.dart';

/// MORPHIC SHOWCASE — SPATIAL HOME (scene 02): the visionOS-style control hub.
///
/// One spatial OBJECT made of four sovereign chromeless glass surfaces on a
/// single composition plane:
///
///   · panel   — the main widget grid (anchor / plane root)
///   · header  — the floating welcome pill above it
///   · sidebar — the icon rail hugging the panel's left edge
///   · rooms   — the room-switcher pill floating below
///
/// Every surface is `chromeless` (no native strip/borders — content fills the
/// window) with a custom `cornerRadius` — a runtime-owned property the SDK's
/// MorphicWindowShape applies, so the window's composited shape IS the rounded
/// content (alpha-glass; backdrop 'none' because an ACCENT blur backdrop is
/// always rectangular — OS boundary). The composition is AUTHORED: the rects
/// below are the scene. The boot root dissolves — the scene IS the app presence.
class SpatialHomeApp extends MorphicApp {
  @override
  String get name => 'Home';

  @override
  bool get dissolveRootIntoScene => true;

  // Authored composition. The panel is the plane root; the pills are composed
  // members (shared activation light, shared drag, owner-chain raise).
  @override
  List<SurfaceSpec> surfaces() => [
        SurfaceSpec.workspace(
          id: 'panel',
          entrypoint: 'homePanelMain',
          x: 440,
          y: 210,
          width: 1040,
          height: 620,
          transparency: 'full_glass',
          backdrop: 'none',
          chromeless: true,
          cornerRadius: 28,
          backend: 'spatial',
          material: 'acrylic',
          materialTint: 0x59FFFFFF,
          elevation: 26,
        ),
        SurfaceSpec.inspector(
          id: 'header',
          entrypoint: 'homeHeaderMain',
          parent: 'panel',
          composed: true,
          x: 550,
          y: 122,
          width: 820,
          height: 64,
          transparency: 'full_glass',
          backdrop: 'none',
          chromeless: true,
          cornerRadius: 32,
          backend: 'spatial',
          material: 'acrylic',
          shape: 'capsule',
          materialTint: 0x59FFFFFF,
          elevation: 18,
        ),
        SurfaceSpec.toolPalette(
          id: 'sidebar',
          entrypoint: 'homeSidebarMain',
          parent: 'panel',
          composed: true,
          x: 356,
          y: 330,
          width: 64,
          height: 380,
          transparency: 'full_glass',
          backdrop: 'none',
          chromeless: true,
          cornerRadius: 32,
          backend: 'spatial',
          material: 'acrylic',
          shape: 'capsule',
          materialTint: 0x59FFFFFF,
          elevation: 18,
        ),
        // RENDERER VALIDATION (Material Correction gate): a perfect circle is
        // the edge-quality probe — aliasing, jitter, alpha seams and shadow
        // mismatch all show on a circle first. Pure material, empty content.
        // Remove once the material finishing is signed off.
        SurfaceSpec.toolPalette(
          id: 'circle_probe',
          entrypoint: 'homeCircleMain',
          parent: 'panel',
          composed: true,
          x: 1530,
          y: 420,
          width: 180,
          height: 180,
          transparency: 'full_glass',
          backdrop: 'none',
          chromeless: true,
          backend: 'spatial',
          material: 'acrylic',
          shape: 'circle',
          materialTint: 0x59FFFFFF,
          elevation: 22,
        ),
        SurfaceSpec.toolPalette(
          id: 'rooms',
          entrypoint: 'homeRoomsMain',
          parent: 'panel',
          composed: true,
          x: 650,
          y: 854,
          width: 620,
          height: 58,
          transparency: 'full_glass',
          backdrop: 'none',
          chromeless: true,
          cornerRadius: 29,
          backend: 'spatial',
          material: 'acrylic',
          shape: 'capsule',
          materialTint: 0x59FFFFFF,
          elevation: 18,
        ),
      ];
}
