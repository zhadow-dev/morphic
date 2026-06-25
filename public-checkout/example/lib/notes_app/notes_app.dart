import '../morphic_app/atmosphere_profile.dart';
import 'package:morphic/morphic.dart';

/// MORPHIC SECOND APP (M2.2) — Notes, declared via the authoring layer.
///
/// This replaces the imperative `_spawnNotes` (manual kind/entrypoint/x/y/parentId/composed for
/// each surface) with a DECLARATIVE surface graph. The editor↔inspector relationship is now data:
/// the inspector names the editor as its `parent` and is `composed` (joins its plane). Boot with
/// `runMorphicApp(app: NotesApp())` — no launcher hacks, no raw channel calls, no C++.
///
/// Entrypoint names are `@pragma('vm:entry-point')` functions exported from main.dart; each runs
/// in its OWN engine (multi-engine truth stays visible — these are not children of a widget tree).
class NotesApp extends MorphicApp {
  @override
  String get name => 'Notes';

  // M2.5 — the per-surface glass mode + backdrop now come from the ONE source (AtmosphereProfile), so
  // spawn-time and content-time presentation can't drift apart. (Non-const list: field access isn't a
  // const expression — harmless, the specs build at boot.)
  @override
  List<SurfaceSpec> surfaces() => [
        SurfaceSpec.workspace(
          id: 'editor',
          entrypoint: 'notesEditorMain',
          x: 160,
          y: 160,
          width: 560,
          height: 460,
          transparency: AtmosphereProfile.editor.transparency,
          backdrop: AtmosphereProfile.editor.backdrop,
        ),
        SurfaceSpec.inspector(
          id: 'inspector',
          entrypoint: 'notesInspectorMain',
          parent: 'editor', // resolved to the editor's native id at spawn
          composed: true, // joins the editor's plane
          x: 740,
          y: 160,
          width: 240,
          height: 460,
          transparency: AtmosphereProfile.inspector.transparency,
          backdrop: AtmosphereProfile.inspector.backdrop,
        ),
        // SC-1 — a real formatting tool palette, COMPOSED under the editor: it joins the editor's
        // composition plane, so the editor + inspector + palette read and move as ONE spatial object
        // (shared activation lighting + shared drag), while staying three independent HWNDs. This is
        // the "main + tool palette compound" — built on the existing CompositionPlane infra, no new
        // runtime. It acts on the ACTIVE document via notes.cmd (§4b-safe data, not input).
        SurfaceSpec.toolPalette(
          id: 'palette',
          entrypoint: 'notesPaletteMain',
          parent: 'editor',
          composed: true,
          x: 1000,
          y: 160,
          width: 158,
          height: 360,
          transparency: AtmosphereProfile.utility.transparency,
          backdrop: AtmosphereProfile.utility.backdrop,
        ),
      ];
}
