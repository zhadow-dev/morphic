import 'package:flutter/material.dart';

import 'editor_pane.dart';

/// PHASE 2 surface entrypoint — runs in its own Flutter engine inside a
/// SurfaceShell. Must be a top-level @pragma('vm:entry-point') function.
@pragma('vm:entry-point')
void editorMain() {
  runApp(const _EditorSurfaceApp());
}

class _EditorSurfaceApp extends StatelessWidget {
  const _EditorSurfaceApp();

  @override
  Widget build(BuildContext context) {
    return const MaterialApp(
      debugShowCheckedModeBanner: false,
      home: EditorSurface(),
    );
  }
}

/// A composition unit that hosts the editor.
///
/// PHASE 1A: this simply wraps the existing [EditorPane]. It is a plain widget,
/// NOT yet a runtime-mounted Morphic surface — that (independent FlutterView /
/// engine per surface) is deferred to the multi-view phase. Keeping it as its
/// own widget now means the eventual surface-root migration is a local change.
class EditorSurface extends StatelessWidget {
  const EditorSurface({super.key});

  @override
  Widget build(BuildContext context) {
    // engineId 0 == the primary shell engine.
    return const EditorPane(engineId: 0);
  }
}
