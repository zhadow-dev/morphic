import 'package:flutter/material.dart';

import 'forensic.dart';
import 'ecology_launcher.dart';
import 'package:morphic/morphic.dart';
import 'notes_app/notes_app.dart';
import 'habitat_app/habitat_app.dart';
import 'spatial_home_app/spatial_home_app.dart';

// PHASE 10.2 — export per-role entrypoints so the native SurfaceShell engines
// can resolve them by name and they survive tree-shaking. Each runs in its own
// engine inside a SurfaceShell HWND. main() runs the ecology launcher (the
// control surface).
export 'workspace_content.dart';
export 'tool_palette_content.dart';
export 'inspector_content.dart';
export 'overlay_content.dart';

// MORPHIC SECOND APP (M2.2) — "Notes" app surfaces. Run their own entrypoints inside
// workspace/inspector/palette-KIND surfaces via the friction-#1 entrypoint override (Dart-only,
// no C++ per surface type). This is the real-app pressure that lets the SDK discover itself.
export 'notes_app/notes_editor.dart';
export 'notes_app/notes_inspector.dart';
export 'notes_app/notes_command_palette.dart';
export 'notes_app/notes_palette.dart';

// MORPHIC SHOWCASE (M2.8) — "Habitat" spatial-native scene entrypoints (each its own engine).
export 'habitat_app/presence_field.dart';
export 'habitat_app/memory_echoes.dart';
export 'habitat_app/ambient_context.dart';

// MORPHIC SHOWCASE — "Spatial Home" (scene 02): the visionOS-style control hub built from
// chromeless rounded glass surfaces on one composition plane.
export 'spatial_home_app/home_panel.dart';
export 'spatial_home_app/home_header.dart';
export 'spatial_home_app/home_sidebar.dart';
export 'spatial_home_app/home_rooms.dart';
export 'spatial_home_app/home_circle.dart';
export 'spatial_home_app/home_test_controls.dart'; // homeSpawnedMain (runtime spawn)

// Legacy entrypoints (still used by stress/soak harnesses).
export 'secondary_main.dart';
export 'navbar_surface.dart';
export 'editor_surface.dart';

// Boot selector. 'home' = the SPATIAL HOME showcase (visionOS-style control hub on chromeless
// glass). 'habitat' (M2.8) = the Habitat presence scene. 'notes' = the SDK test/validation app
// (the friction driver; still fully working). 'ecology' = the demo/lab launcher. One-line switch.
const String _kBootApp = 'home';

void main() {
  Forensic.init();
  if (_kBootApp == 'home') {
    Forensic.log('BOOT', 'main() — runMorphicApp(app: SpatialHomeApp())');
    runMorphicApp(app: SpatialHomeApp());
    return;
  }
  if (_kBootApp == 'habitat') {
    Forensic.log('BOOT', 'main() — runMorphicApp(app: HabitatApp())');
    runMorphicApp(app: HabitatApp());
    return;
  }
  if (_kBootApp == 'notes') {
    Forensic.log('BOOT', 'main() — runMorphicApp(app: NotesApp())');
    runMorphicApp(app: NotesApp());
    return;
  }
  Forensic.log('BOOT', 'main() entered — ecology launcher');

  final binding = WidgetsFlutterBinding.ensureInitialized();
  Forensic.log('BOOT', 'WidgetsFlutterBinding.ensureInitialized done');

  binding.addPostFrameCallback((_) {
    Forensic.log('FRAME', 'first widget frame rendered (post-frame callback)');
  });

  Forensic.log('BOOT', 'calling runApp(EcologyLauncher)');
  runApp(const EcologyLauncher());
}
