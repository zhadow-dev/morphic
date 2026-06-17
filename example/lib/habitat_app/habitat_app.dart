import '../morphic_app/atmosphere_profile.dart';
import 'package:morphic/morphic.dart';

/// MORPHIC SHOWCASE (M2.8) — THE HABITAT, scene 01: the first SPATIAL-NATIVE demo.
///
/// Ontology: continuous cognitive PRESENCE, **no primary task**. A presence FIELD (anchor) with two
/// orbital companions — memory echoes + ambient context — composed into ONE atmospheric plane (shared
/// activation lighting + shared drag, three sovereign HWNDs). Built on the SAME authoring layer as
/// Notes; the difference is ONTOLOGY, not architecture — content whose purpose is ambient/environmental
/// rather than document-productivity. The milestone is perceptual: a viewer reads "this isn't an app".
///
/// Notes remains the SDK test/validation app; this is the showcase. Boot via `_kBootApp = 'habitat'`.
class HabitatApp extends MorphicApp {
  @override
  String get name => 'Habitat';

  // M2.8 — a spatial-native scene IS its own app-presence; the boot-root chip
  // would be a second anchor competing with the real one. The root hides itself
  // natively before spawning, then closes once the scene is up.
  @override
  bool get dissolveRootIntoScene => true;

  // AUTHORED composition = the SNAPSHOT (zoom 1.0). The habitat spawns as a legible WINDOWED
  // composition — presence + two companions in a clear orbital relationship — and the presence then
  // performs a SCENE ZOOM (camera move) toward filling the field, scaling the WHOLE composition
  // proportionally FROM this authored snapshot (see presence_field + SceneZoomController). So immersion
  // is a camera DISTANCE, not a window state, and the anchor + companions scale together (no popups).
  // These rects are the AUTHORED relationship; the zoom is derived from them, never the other way.
  @override
  List<SurfaceSpec> surfaces() => [
        SurfaceSpec.workspace(
          id: 'presence',
          entrypoint: 'presenceFieldMain',
          x: 60,
          y: 90,
          width: 820,
          height: 560,
          transparency: AtmosphereProfile.presence.transparency,
          backdrop: AtmosphereProfile.presence.backdrop,
        ),
        SurfaceSpec.inspector(
          id: 'echoes',
          entrypoint: 'memoryEchoesMain',
          parent: 'presence', // resolved to the presence field's native id at spawn
          composed: true, // joins the presence plane (lights/drifts as one); floats ABOVE (owned)
          x: 900,
          y: 96,
          width: 250,
          height: 300,
          transparency: AtmosphereProfile.echo.transparency,
          backdrop: AtmosphereProfile.echo.backdrop,
        ),
        SurfaceSpec.toolPalette(
          id: 'context',
          entrypoint: 'ambientContextMain',
          parent: 'presence',
          composed: true,
          x: 900,
          y: 430,
          width: 250,
          height: 250,
          transparency: AtmosphereProfile.ambient.transparency,
          backdrop: AtmosphereProfile.ambient.backdrop,
        ),
      ];
}
