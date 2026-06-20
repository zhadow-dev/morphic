import 'package:flutter/material.dart';
import 'package:morphic/morphic.dart';

import 'surfaces.dart';

ThemeData _theme() => ThemeData(
  brightness: Brightness.dark,
  colorSchemeSeed: const Color(0xFF7C5CFF),
  scaffoldBackgroundColor: const Color(0xFF0E1017),
  useMaterial3: true,
);

// Each surface (window) is launched by name. `@pragma('vm:entry-point')` keeps
// these from being tree-shaken so the runtime can find them.

@pragma('vm:entry-point')
void notesList() => runApp(
  MaterialApp(
    theme: _theme(),
    debugShowCheckedModeBanner: false,
    home: const NotesListSurface(),
  ),
);

@pragma('vm:entry-point')
void noteEditor() => runApp(
  MaterialApp(
    theme: _theme(),
    debugShowCheckedModeBanner: false,
    home: const EditorSurface(),
  ),
);

@pragma('vm:entry-point')
void noteInspector() => runApp(
  MaterialApp(
    theme: _theme(),
    debugShowCheckedModeBanner: false,
    home: const InspectorSurface(),
  ),
);

/// Declares the three windows that boot with the app.
class NotesApp extends MorphicApp {
  @override
  String get name => 'Morphic Notes';

  @override
  List<SurfaceSpec> surfaces() => const [
    SurfaceSpec.workspace(
      id: 'list',
      entrypoint: 'notesList',
      x: 120,
      y: 120,
      width: 320,
      height: 540,
    ),
    SurfaceSpec.workspace(
      id: 'editor',
      entrypoint: 'noteEditor',
      x: 460,
      y: 120,
      width: 560,
      height: 540,
    ),
    SurfaceSpec.inspector(
      id: 'inspector',
      entrypoint: 'noteInspector',
      parent: 'editor',
      x: 1040,
      y: 120,
      width: 280,
      height: 540,
    ),
  ];
}

void main() => runMorphicApp(app: NotesApp());
