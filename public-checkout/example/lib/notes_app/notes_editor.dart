import 'dart:math' as math;

import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'package:morphic/morphic.dart';
import '../morphic_app/atmosphere_profile.dart';
import '../morphic_app/morphic_spatial_theme.dart';
import 'app_bus.dart';

/// AUTHORED SCENE COMPOSITION knobs (M2.7) — content-only; tune the maximized editor's read HERE.
/// These never move/resize the WINDOW (presentation only): they compose the editor's CONTENT into an
/// authored writing column + open context field when the surface is wide (maximized). Below the
/// threshold the editor is a small sovereign window and fills normally.
const double kSceneThreshold = 1000; // px: above this width the editor composes as the immersive scene
const double kWritingMeasure = 760; // px: max reading column in scene (a measure, not near-full-width)
const double kSceneMeasureFraction = 0.6; // scene column width = min(width * this, kWritingMeasure)
const double kSceneInset = 48; // authored left breathing for the content column in scene (was 24)

/// MORPHIC SECOND APP (M2.2) — "Notes", a real productivity surface, NOT a demo widget.
///
/// This is the first surface of the second app: a working markdown-ish note editor with real
/// text state. It is spawned as a `workspace`-KIND surface (grounded/Standard behavior) but runs
/// THIS entrypoint as its CONTENT — proving the M2.2 friction-#1 fix (content/kind decoupling)
/// end-to-end, with zero C++ per surface type. It participates in the plane system via
/// PlaneDimmable like any surface.
///
/// Purpose: put REAL pressure on the ontology — real keyboard focus/IME (does typing reach THIS
/// surface's HWND? = §4b in practice), real per-surface state, real workflow — so the authoring
/// patterns the SDK should expose are DISCOVERED here, not invented.
@pragma('vm:entry-point')
void notesEditorMain() {
  runApp(const _NotesEditorApp());
}

class _NotesEditorApp extends StatelessWidget {
  const _NotesEditorApp();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        scaffoldBackgroundColor: Colors.transparent, // M2.4 B — let the glass field show
      ),
      home: const MorphicAtmosphere(
          profile: AtmosphereProfile.editor, child: _NotesEditor()),
    );
  }
}

class _NotesEditor extends StatefulWidget {
  const _NotesEditor();

  @override
  State<_NotesEditor> createState() => _NotesEditorState();
}

class _NotesEditorState extends State<_NotesEditor> {
  final _title = TextEditingController(text: 'Untitled note');
  final _body = TextEditingController(
      text: '# Heading\n\nStart typing. This is a real editor surface running its own '
          'Dart entrypoint inside a Morphic workspace.\n\n- multi-surface\n- real state\n'
          '- real keyboard focus');
  final _bodyFocus = FocusNode();
  final _titleFocus = FocusNode();
  late final String _docId;
  String? _surfaceId; // this editor's own native surface id (M2.1D handshake)
  bool _maximized = false; // M2.3E — native window state (for the maximize/restore icon)
  final _ecology = EcologyController();

  // M2.2E — Ctrl+K opens the command palette as its OWN overlay surface (not embedded here).
  void _openPalette() {
    _ecology.spawnSurface(
      kind: 'overlay',
      entrypoint: 'commandPaletteMain',
      x: 480,
      y: 220,
      width: 420,
      height: 360,
    );
  }

  // M2.3A — the app owns "new note" itself (was the ecology launcher's job). Each new note is an
  // ADDITIONAL editor surface = its own engine = its own document. Cascade-offset so it doesn't
  // land exactly on this one. This is what lets Notes boot with NO launcher and still grow.
  void _newNote() {
    final off = 28 * (DateTime.now().millisecondsSinceEpoch ~/ 7 % 8);
    _ecology.spawnSurface(
      kind: 'workspace',
      entrypoint: 'notesEditorMain',
      x: 200 + off,
      y: 170 + off,
      width: 560,
      height: 460,
    );
  }

  @override
  void initState() {
    super.initState();
    // Each editor surface IS a document, with a stable identity. Multiple editors = multiple docs.
    _docId = 'doc_${identityHashCode(this) % 100000}';
    // Learn this surface's OWN native id (M2.1D handshake), then register the doc WITH it, so the
    // session maps surface→doc and the runtime's `surface.destroyed` reliably cleans up the doc
    // even on a hard HWND destroy (Dart dispose may not run). docClosed below stays as the fast path.
    MorphicSurface.currentId().then((surfaceId) {
      if (!mounted) return;
      _surfaceId = surfaceId;
      _announceOpen();
    });
    // M2.2E.1 — answer roster sync. A late-born session (a command palette / tool palette) broadcasts
    // notes.syncRequest because the bus has no retention; we re-announce this doc so it appears
    // immediately instead of only after a manual focus. If we currently hold focus, also re-announce
    // ACTIVE so a freshly-spawned palette knows which doc to target at boot. The mounted+id guard in
    // _announceOpen stops a disposed editor from resurrecting a ghost doc (its closure lingers in
    // AppBus, which has no unsubscribe yet — logged as friction, not yet worth building unsubscribe for).
    AppBus.on('notes.syncRequest', (_) {
      _announceOpen();
      if (mounted && (_bodyFocus.hasFocus || _titleFocus.hasFocus)) _broadcastActive();
    });
    // SC-1 — the formatting palette is its own surface; it can't touch our text, so it broadcasts a
    // COMMAND (data, §4b) targeted by docId. We apply it to OUR controller iff it's for us. The
    // palette having OS foreground while this lands on us is the point: semantic-active ≠ foreground.
    AppBus.on('notes.cmd', (p) {
      if (!mounted || p['docId'] != _docId) return;
      final action = p['action'] as String?;
      if (action != null) _applyCmd(action);
    });
    // M2.3E — reflect this window's native maximize/restore state in the chrome icon.
    MorphicSurface.onWindowState((max, min) {
      if (mounted) setState(() => _maximized = max);
    });
    // §4b-SAFE active-document tracking: this editor announces ITSELF as the active doc when its
    // OWN content gains keyboard focus — focus the OS already granted by making this HWND
    // foreground. The inspector passively reflects whoever last announced. NOTHING else (no plane,
    // no visual-active state) decides the keyboard target; the focused surface self-reports. We
    // only ever broadcast DATA — routing input from here would be the §4b violation.
    _bodyFocus.addListener(_onFocusChange);
    _titleFocus.addListener(_onFocusChange);
  }

  void _onFocusChange() {
    if (_bodyFocus.hasFocus || _titleFocus.hasFocus) _broadcastActive();
  }

  // Announce this surface's doc EXISTENCE (notes.docOpened). Called once when identity lands and
  // again whenever a late session asks (notes.syncRequest). Guarded: a disposed editor (mounted ==
  // false) or one without its surface id yet stays silent, so it can never resurrect a ghost doc.
  void _announceOpen() {
    final sid = _surfaceId;
    if (!mounted || sid == null) return;
    AppBus.broadcast('notes.docOpened',
        {'docId': _docId, 'surfaceId': sid, 'title': _title.text});
  }

  List<String> get _outline => _body.text
      .split('\n')
      .where((l) => l.trimLeft().startsWith('#'))
      .map((l) => l.trim())
      .toList();

  void _onChanged() {
    setState(() {});
    _broadcastActive(); // typing implies this doc is the active one
  }

  void _broadcastActive() {
    AppBus.broadcast('notes.activeDoc', {
      'docId': _docId,
      'title': _title.text,
      'words': _words,
      'outline': _outline,
    });
  }

  // SC-1 — apply a formatting command from the tool palette to THIS body. Line-oriented commands
  // operate on the line at the caret; 'bold' wraps the selection. We mutate our own controller —
  // never the palette's, never anyone's input.
  void _applyCmd(String action) {
    final text = _body.text;
    final sel = _body.selection;
    final caret = (sel.isValid ? sel.baseOffset : text.length).clamp(0, text.length);

    if (action == 'bold') {
      if (sel.isValid && !sel.isCollapsed) {
        final inner = text.substring(sel.start, sel.end);
        final out = text.replaceRange(sel.start, sel.end, '**$inner**');
        _body.value = TextEditingValue(
            text: out, selection: TextSelection.collapsed(offset: sel.end + 4));
      } else {
        final out = text.replaceRange(caret, caret, '****');
        _body.value = TextEditingValue(
            text: out, selection: TextSelection.collapsed(offset: caret + 2));
      }
      _onChanged();
      return;
    }

    // Line-oriented: find the line containing the caret.
    final lineStart = text.lastIndexOf('\n', caret - 1) + 1;
    var lineEnd = text.indexOf('\n', caret);
    if (lineEnd == -1) lineEnd = text.length;
    final line = text.substring(lineStart, lineEnd);

    String next;
    switch (action) {
      case 'h1':
        next = _heading(line, 1);
        break;
      case 'h2':
        next = _heading(line, 2);
        break;
      case 'h3':
        next = _heading(line, 3);
        break;
      case 'bullet':
        next = _togglePrefix(line, '- ');
        break;
      case 'checkbox':
        next = _togglePrefix(line, '- [ ] ');
        break;
      default:
        return;
    }
    final out = text.replaceRange(lineStart, lineEnd, next);
    _body.value = TextEditingValue(
        text: out, selection: TextSelection.collapsed(offset: lineStart + next.length));
    _onChanged();
  }

  // Replace any existing heading level with `level` hashes (idempotent re-heading).
  String _heading(String line, int level) {
    final stripped = line.replaceFirst(RegExp(r'^#{1,6}\s*'), '');
    return '${'#' * level} $stripped';
  }

  // Toggle a line prefix; strip an existing bullet/checkbox first so they don't stack.
  String _togglePrefix(String line, String prefix) {
    if (line.startsWith(prefix)) return line.substring(prefix.length);
    final cleaned = line.replaceFirst(RegExp(r'^(- \[ \] |- )'), '');
    return '$prefix$cleaned';
  }

  @override
  void dispose() {
    AppBus.broadcast('notes.docClosed', {'docId': _docId}); // best-effort (see initState note)
    _bodyFocus.removeListener(_onFocusChange);
    _titleFocus.removeListener(_onFocusChange);
    _title.dispose();
    _body.dispose();
    _bodyFocus.dispose();
    _titleFocus.dispose();
    super.dispose();
  }

  int get _words =>
      _body.text.trim().isEmpty ? 0 : _body.text.trim().split(RegExp(r'\s+')).length;

  // M2.3E — a compact titlebar window-control button (surface-local action on tap).
  Widget _winBtn(IconData icon, String tip, VoidCallback onTap, {Color? color}) {
    return Tooltip(
      message: tip,
      child: GestureDetector(
        onTap: onTap,
        child: Padding(
          padding: const EdgeInsets.symmetric(horizontal: 5),
          child: Icon(icon, size: 14, color: color ?? const Color(0xFF6E7A8A)),
        ),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return CallbackShortcuts(
      bindings: {
        const SingleActivator(LogicalKeyboardKey.keyK, control: true): _openPalette,
        const SingleActivator(LogicalKeyboardKey.keyN, control: true): _newNote,
      },
      child: Scaffold(
        backgroundColor: Colors.transparent, // field (environmental gradient) comes from the profile
        body: LayoutBuilder(
          builder: (context, c) {
            // AUTHORED SCENE COMPOSITION (M2.7). Below kSceneThreshold the editor is a small sovereign
            // window: its content fills edge-to-edge (correct — a small window). At/above it (maximized =
            // the immersive scene) the editor STOPS reading as a full-width "maximized app": title,
            // writing, and status compose as ONE authored column floating left in the field, opening a
            // deliberate CONTEXT FIELD on the right — atmospheric depth the companion cluster floats in.
            // Presentation ONLY: the WINDOW still fills the work area (no resize); this is content layout.
            final bool scene = c.maxWidth > kSceneThreshold;
            final double measure = scene
                ? math.min(c.maxWidth * kSceneMeasureFraction, kWritingMeasure)
                : c.maxWidth;
            final double leftInset = scene ? kSceneInset : MorphicSpace.margin;
            return Stack(
              fit: StackFit.expand,
              children: [
                // CONTEXT-FIELD zoning (scene only): a subtle depth veil deepening toward the lower-right
                // (with the field's upper-left light), so the region the companions float in reads as
                // authored atmospheric DEPTH, not dead space. Luminance only; IgnorePointer; off otherwise.
                if (scene)
                  const IgnorePointer(
                    child: DecoratedBox(
                      decoration: BoxDecoration(gradient: MorphicGlass.sceneContextDepth),
                    ),
                  ),
                Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    _titleStrip(measure, leftInset, scene),
                    Expanded(child: _bodyEditor(measure, leftInset, scene)),
                    _statusLine(leftInset),
                  ],
                ),
              ],
            );
          },
        ),
      ),
    );
  }

  // Title strip — dissolved chrome (no hard separator). The new-note + window controls stay pinned
  // top-right (it IS a window — keep that). In scene, the title cluster is constrained to the writing
  // `measure` so it sits above its column instead of spanning full width like app chrome; a Spacer
  // opens the gap to the controls.
  Widget _titleStrip(double measure, double leftInset, bool scene) {
    final titleCluster = Row(
      children: [
        const Icon(Icons.edit_note, size: 16, color: Color(0xFF6E7A8A)),
        const SizedBox(width: 8),
        Expanded(
          child: TextField(
            controller: _title,
            focusNode: _titleFocus,
            style: MorphicType.title,
            decoration: const InputDecoration(
              isDense: true,
              border: InputBorder.none,
              hintText: 'Title',
            ),
            onChanged: (_) => _onChanged(),
          ),
        ),
      ],
    );
    return Container(
      padding: EdgeInsets.fromLTRB(leftInset, 16, 16, 10),
      child: Row(
        children: [
          if (scene) ...[
            SizedBox(width: measure, child: titleCluster),
            const Spacer(),
          ] else
            Expanded(child: titleCluster),
          // M2.3A — app-owned "new note" (also Ctrl+N). Keeps Notes self-sufficient sans launcher.
          Tooltip(
            message: 'New note  (Ctrl+N)',
            child: GestureDetector(
              onTap: _newNote,
              child: const Padding(
                padding: EdgeInsets.only(left: 8),
                child: Icon(Icons.add, size: 16, color: Color(0xFF6E7A8A)),
              ),
            ),
          ),
          // M2.3E — native window controls (surface-local; double-click titlebar also maximizes).
          const SizedBox(width: 10),
          _winBtn(Icons.remove, 'Minimize', MorphicSurface.minimize),
          _winBtn(_maximized ? Icons.filter_none : Icons.crop_square_outlined,
              _maximized ? 'Restore' : 'Maximize', MorphicSurface.toggleMaximize),
          _winBtn(Icons.close, 'Close', MorphicSurface.close,
              color: const Color(0xFFB0707A)),
        ],
      ),
    );
  }

  // Body editor — the writing column. In scene it's a fixed authored `measure` floating left, leaving
  // the remaining width OPEN as the context field (companions float there). Below scene it fills.
  Widget _bodyEditor(double measure, double leftInset, bool scene) {
    final writing = TextField(
      controller: _body,
      focusNode: _bodyFocus,
      autofocus: true,
      maxLines: null,
      expands: true,
      textAlignVertical: TextAlignVertical.top,
      cursorColor: MorphicColors.accent,
      style: MorphicType.body,
      decoration: const InputDecoration(
        border: InputBorder.none,
        hintText: 'Write…',
      ),
      onChanged: (_) => _onChanged(),
    );
    return Padding(
      padding: EdgeInsets.fromLTRB(leftInset, 8, 16, 12),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          if (scene) ...[
            SizedBox(width: measure, child: writing),
            const Expanded(child: SizedBox.shrink()), // the open context field
          ] else
            Expanded(child: writing),
        ],
      ),
    );
  }

  // Status line — a quiet whisper aligned to the content column, not a window footer.
  Widget _statusLine(double leftInset) {
    return Container(
      height: 26,
      padding: EdgeInsets.fromLTRB(leftInset, 0, 16, 6),
      alignment: Alignment.centerLeft,
      child: Text('$_words words · markdown', style: MorphicType.whisper),
    );
  }
}
