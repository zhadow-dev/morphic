import 'dart:async';

import 'package:flutter/material.dart';

import 'package:morphic/morphic.dart';
import 'notes_app/notes_app.dart';

/// PHASE 10.2 / 11–12 — EcologyLauncher.
///
/// The control surface for the surface ecology. Spawns, observes, and destroys
/// surfaces through the EcologyController. This is NOT a workspace — it is the
/// meta-workspace that orchestrates the ecology. It runs in the primary engine
/// (the 'main' entrypoint surface).
///
/// PHASE 11/12 — the SPAWN OPTIONS panel sets per-surface appearance + composition
/// (corners / shadow / backdrop / shared-drag) applied to the next spawn.
class EcologyLauncher extends StatefulWidget {
  const EcologyLauncher({super.key});

  @override
  State<EcologyLauncher> createState() => _EcologyLauncherState();
}

class _EcologyLauncherState extends State<EcologyLauncher> {
  final _ecology = EcologyController();
  List<Map<String, dynamic>> _surfaces = [];
  List<Map<String, dynamic>> _workspaces = [];
  String _currentWorkspace = '';
  final List<String> _log = [];
  Timer? _refreshTimer;

  // PHASE 11/12 — per-surface feature selections applied to the NEXT spawn.
  String _corners = 'rounded';
  bool _shadow = true;
  String _backdrop = 'none';
  bool _composed = false;

  @override
  void initState() {
    super.initState();
    _refreshTimer = Timer.periodic(
        const Duration(seconds: 2), (_) => _refreshSummary());
    _refreshSummary();
  }

  @override
  void dispose() {
    _refreshTimer?.cancel();
    super.dispose();
  }

  void _addLog(String msg) {
    setState(() {
      _log.insert(0, '${DateTime.now().toString().substring(11, 19)} $msg');
      if (_log.length > 50) _log.removeLast();
    });
  }

  Future<void> _refreshSummary() async {
    try {
      final summary = await _ecology.getSummary();
      if (!mounted) return;
      setState(() {
        final rawSurfaces = summary['surfaces'] as List? ?? [];
        _surfaces = rawSurfaces.map((s) {
          final m = s as Map;
          return Map<String, dynamic>.fromEntries(
              m.entries.map((e) => MapEntry(e.key.toString(), e.value)));
        }).toList();

        final rawWorkspaces = summary['workspaces'] as List? ?? [];
        _workspaces = rawWorkspaces.map((w) {
          final m = w as Map;
          return Map<String, dynamic>.fromEntries(
              m.entries.map((e) => MapEntry(e.key.toString(), e.value)));
        }).toList();

        _currentWorkspace =
            (summary['currentWorkspace'] as String?) ?? '';
      });
    } catch (e) {
      // Ignore — summary may fail during startup.
    }
  }

  Future<void> _spawnWorkspace() async {
    _addLog('[SPAWN] workspace requested');
    final id = await _ecology.spawnWorkspace(
        corners: _corners,
        shadow: _shadow,
        backdrop: _backdrop,
        composed: _composed);
    _addLog('[SPAWN] workspace → ${id ?? 'REJECTED'}');
    _refreshSummary();
  }

  // MORPHIC SECOND APP (M2.2) — spawn the Notes editor: a `workspace`-KIND surface (grounded
  // Standard behavior) running the SECOND APP's own `notesEditorMain` content via the friction-#1
  // entrypoint override. No C++ per surface type — pure Dart. (Proper notes-app boot is next.)
  // M2.2B — the SAME Notes app, now spawned through the authoring layer: one declarative call
  // (NotesApp declares its editor+inspector surface graph as SurfaceSpecs; spawnApp resolves the
  // parent relationship + composes). This replaced ~25 lines of imperative kind/entrypoint/x/y/
  // parentId plumbing — the first proof the authoring layer removes real boilerplate.
  Future<void> _spawnNotes() async {
    _addLog('[SPAWN] Notes app via spawnApp(NotesApp())');
    final ids = await spawnApp(NotesApp());
    _addLog('[SPAWN] Notes app → ${ids.length} surfaces: ${ids.values.join(', ')}');
    _refreshSummary();
  }

  // M2.2C — multiple documents. Each "new note" is an ADDITIONAL editor surface (its own engine =
  // its own document). The shared inspector retargets to whichever editor is focused (active-doc
  // tracking via AppBus). Spawned raw for now — the boilerplate (offset math, no doc registry,
  // no central active state) is exactly the pain that EARNS NotesSession/AppSession next.
  Future<void> _newNote() async {
    final off = 30 * (_surfaces.length % 8);
    _addLog('[SPAWN] new note (additional editor)');
    final id = await _ecology.spawnSurface(
      kind: 'workspace',
      entrypoint: 'notesEditorMain',
      x: 200 + off,
      y: 180 + off,
      width: 560,
      height: 460,
    );
    _addLog('[SPAWN] new note → ${id ?? 'REJECTED'}');
    _refreshSummary();
  }

  Future<void> _spawnToolPalette() async {
    // Find first workspace surface to use as parent.
    final ws = _surfaces.where((s) => s['kind'] == 'workspace').toList();
    if (ws.isEmpty) {
      _addLog('[POLICY] no workspace to parent tool palette');
      return;
    }
    final parentId = ws.first['id'] as String;
    _addLog('[SPAWN] tool_palette parent=$parentId');
    final id = await _ecology.spawnToolPalette(
        parentId: parentId,
        corners: _corners,
        shadow: _shadow,
        backdrop: _backdrop,
        composed: _composed);
    _addLog('[SPAWN] tool_palette → ${id ?? 'REJECTED'}');
    _refreshSummary();
  }

  Future<void> _spawnInspector() async {
    final ws = _surfaces.where((s) => s['kind'] == 'workspace').toList();
    if (ws.isEmpty) {
      _addLog('[POLICY] no workspace to parent inspector');
      return;
    }
    final parentId = ws.first['id'] as String;
    _addLog('[SPAWN] inspector parent=$parentId');
    final id = await _ecology.spawnInspector(
        parentId: parentId,
        corners: _corners,
        shadow: _shadow,
        backdrop: _backdrop,
        composed: _composed);
    _addLog('[SPAWN] inspector → ${id ?? 'REJECTED'}');
    _refreshSummary();
  }

  Future<void> _spawnOverlay() async {
    _addLog('[SPAWN] overlay requested');
    final id = await _ecology.spawnOverlay(
        corners: _corners,
        shadow: _shadow,
        backdrop: _backdrop,
        composed: _composed);
    _addLog('[SPAWN] overlay → ${id ?? 'REJECTED'}');
    _refreshSummary();
  }

  Future<void> _destroySurface(String id) async {
    _addLog('[DESTROY] $id');
    final ok = await _ecology.destroySurface(id);
    _addLog('[DESTROY] $id → ${ok ? 'OK' : 'FAILED'}');
    _refreshSummary();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        scaffoldBackgroundColor: const Color(0xFF0d1117),
      ),
      home: Scaffold(
        backgroundColor: const Color(0xFF0d1117),
        body: Column(
          children: [
            // Header
            Container(
              height: 36,
              padding: const EdgeInsets.symmetric(horizontal: 14),
              decoration: const BoxDecoration(
                color: Color(0xFF161b22),
                border: Border(
                    bottom: BorderSide(color: Color(0xFF30363d))),
              ),
              child: Row(
                children: [
                  Container(
                    width: 8, height: 8,
                    decoration: BoxDecoration(
                      shape: BoxShape.circle,
                      gradient: LinearGradient(
                        colors: [
                          const Color(0xFF58a6ff),
                          const Color(0xFFd2a8ff),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(width: 10),
                  const Text('ECOLOGY',
                      style: TextStyle(
                          color: Color(0xFFe6edf3),
                          fontSize: 11,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 1.5)),
                  const Spacer(),
                  Text(
                      '${_surfaces.length} surfaces · '
                      '${_workspaces.length} workspaces',
                      style: const TextStyle(
                          color: Color(0xFF6e7681),
                          fontSize: 9,
                          fontFamily: 'Consolas')),
                ],
              ),
            ),
            // Spawn buttons
            Container(
              padding: const EdgeInsets.all(10),
              child: Row(
                children: [
                  _spawnButton(
                      'Workspace', const Color(0xFF58a6ff), _spawnWorkspace),
                  const SizedBox(width: 6),
                  _spawnButton(
                      'Palette', const Color(0xFFf0883e), _spawnToolPalette),
                  const SizedBox(width: 6),
                  _spawnButton(
                      'Inspector', const Color(0xFFd2a8ff), _spawnInspector),
                  const SizedBox(width: 6),
                  _spawnButton(
                      'Overlay', const Color(0xFFf7d731), _spawnOverlay),
                ],
              ),
            ),
            // MORPHIC SECOND APP (M2.2) — spawns a real Notes editor surface running its own
            // Dart entrypoint (friction-#1 fix). Visually separated from the ecology demo spawns.
            Container(
              padding: const EdgeInsets.fromLTRB(10, 0, 10, 10),
              child: Row(
                children: [
                  _spawnButton(
                      'Notes app', const Color(0xFF3fb950), _spawnNotes),
                  const SizedBox(width: 6),
                  _spawnButton(
                      'New note', const Color(0xFF2ea043), _newNote),
                ],
              ),
            ),
            // PHASE 11/12 — per-surface spawn options.
            _controlsPanel(),
            // Divider
            const Divider(height: 1, color: Color(0xFF30363d)),
            // Surface list
            Expanded(
              flex: 3,
              child: _surfaces.isEmpty
                  ? const Center(
                      child: Text('No surfaces yet',
                          style: TextStyle(
                              color: Color(0xFF484f58), fontSize: 11)))
                  : ListView.builder(
                      padding: const EdgeInsets.symmetric(vertical: 4),
                      itemCount: _surfaces.length,
                      itemBuilder: (ctx, i) => _surfaceRow(_surfaces[i]),
                    ),
            ),
            // Divider
            Container(
              height: 22,
              padding: const EdgeInsets.symmetric(horizontal: 10),
              decoration: const BoxDecoration(
                color: Color(0xFF161b22),
                border: Border(
                    top: BorderSide(color: Color(0xFF30363d)),
                    bottom: BorderSide(color: Color(0xFF30363d))),
              ),
              child: const Row(
                children: [
                  Text('SEMANTIC LOG',
                      style: TextStyle(
                          color: Color(0xFF6e7681),
                          fontSize: 7,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 1)),
                ],
              ),
            ),
            // Log panel
            Expanded(
              flex: 2,
              child: ListView.builder(
                padding: const EdgeInsets.all(6),
                itemCount: _log.length,
                itemBuilder: (ctx, i) {
                  final line = _log[i];
                  Color color = const Color(0xFF6e7681);
                  if (line.contains('[SPAWN]')) {
                    color = const Color(0xFF3fb950);
                  } else if (line.contains('[DESTROY]')) {
                    color = const Color(0xFFf85149);
                  } else if (line.contains('[POLICY]')) {
                    color = const Color(0xFFf0883e);
                  }
                  return Text(line,
                      style: TextStyle(
                          color: color,
                          fontSize: 9,
                          fontFamily: 'Consolas',
                          height: 1.5));
                },
              ),
            ),
          ],
        ),
      ),
    );
  }

  // PHASE 11/12 — spawn options panel. Selections apply to the NEXT surface spawned.
  Widget _controlsPanel() {
    return Container(
      padding: const EdgeInsets.fromLTRB(10, 0, 10, 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text('SPAWN OPTIONS',
              style: TextStyle(
                  color: Color(0xFF6e7681),
                  fontSize: 7,
                  fontWeight: FontWeight.w700,
                  letterSpacing: 1)),
          const SizedBox(height: 6),
          _segmented('corners', const ['default', 'rounded', 'small', 'square'],
              _corners, (v) => setState(() => _corners = v)),
          const SizedBox(height: 5),
          _segmented('backdrop', const ['none', 'mica', 'acrylic', 'tabbed'],
              _backdrop, (v) => setState(() => _backdrop = v)),
          const SizedBox(height: 6),
          Row(
            children: [
              Expanded(
                  child: _toggle(
                      'shadow', _shadow, (v) => setState(() => _shadow = v))),
              const SizedBox(width: 8),
              Expanded(
                  child: _toggle('shared drag*', _composed,
                      (v) => setState(() => _composed = v))),
            ],
          ),
          const SizedBox(height: 3),
          const Text(
              '* shared-drag now works: spawn a palette/inspector with this ON,\n'
              'then drag the workspace → members follow (12D). backdrop sets the\n'
              'DWM attr but stays hidden behind opaque content (full acrylic n/a).',
              style: TextStyle(
                  color: Color(0xFF6e7681),
                  fontSize: 7,
                  fontStyle: FontStyle.italic,
                  height: 1.4)),
        ],
      ),
    );
  }

  Widget _segmented(String label, List<String> options, String value,
      ValueChanged<String> onChange) {
    return Row(
      crossAxisAlignment: CrossAxisAlignment.center,
      children: [
        SizedBox(
          width: 54,
          child: Text(label,
              style: const TextStyle(
                  color: Color(0xFF8b949e),
                  fontSize: 9,
                  fontFamily: 'Consolas')),
        ),
        Expanded(
          child: Wrap(
            spacing: 4,
            runSpacing: 4,
            children: options.map((o) {
              final sel = o == value;
              return GestureDetector(
                onTap: () => onChange(o),
                child: Container(
                  padding:
                      const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                  decoration: BoxDecoration(
                    color: sel
                        ? const Color(0xFF1f6feb)
                        : const Color(0xFF161b22),
                    borderRadius: BorderRadius.circular(4),
                    border: Border.all(
                        color: sel
                            ? const Color(0xFF1f6feb)
                            : const Color(0xFF30363d)),
                  ),
                  child: Text(o,
                      style: TextStyle(
                          color:
                              sel ? Colors.white : const Color(0xFF8b949e),
                          fontSize: 8,
                          fontWeight: FontWeight.w600)),
                ),
              );
            }).toList(),
          ),
        ),
      ],
    );
  }

  Widget _toggle(String label, bool value, ValueChanged<bool> onChange) {
    return GestureDetector(
      onTap: () => onChange(!value),
      child: Container(
        padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 5),
        decoration: BoxDecoration(
          color: value
              ? const Color(0xFF238636).withOpacity(0.15)
              : const Color(0xFF161b22),
          borderRadius: BorderRadius.circular(4),
          border: Border.all(
              color: value
                  ? const Color(0xFF238636)
                  : const Color(0xFF30363d)),
        ),
        child: Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            Text(label,
                style: TextStyle(
                    color: value
                        ? const Color(0xFF3fb950)
                        : const Color(0xFF8b949e),
                    fontSize: 9,
                    fontWeight: FontWeight.w600)),
            Icon(value ? Icons.toggle_on : Icons.toggle_off,
                size: 16,
                color: value
                    ? const Color(0xFF3fb950)
                    : const Color(0xFF484f58)),
          ],
        ),
      ),
    );
  }

  Widget _spawnButton(String label, Color color, VoidCallback onTap) {
    return Expanded(
      child: GestureDetector(
        onTap: onTap,
        child: Container(
          height: 32,
          decoration: BoxDecoration(
            color: color.withOpacity(0.1),
            borderRadius: BorderRadius.circular(6),
            border: Border.all(color: color.withOpacity(0.3)),
          ),
          child: Center(
            child: Text('+ $label',
                style: TextStyle(
                    color: color,
                    fontSize: 10,
                    fontWeight: FontWeight.w600)),
          ),
        ),
      ),
    );
  }

  Widget _surfaceRow(Map<String, dynamic> surface) {
    final id = surface['id'] as String? ?? '?';
    final kind = surface['kind'] as String? ?? '?';
    final workspace = surface['workspace'] as String? ?? '';
    final parent = surface['parent'] as String? ?? '';
    final followsParent = surface['followsParent'] == true;
    final persistent = surface['persistent'] == true;

    Color kindColor;
    switch (kind) {
      case 'workspace':
        kindColor = const Color(0xFF58a6ff);
        break;
      case 'tool_palette':
        kindColor = const Color(0xFFf0883e);
        break;
      case 'inspector':
        kindColor = const Color(0xFFd2a8ff);
        break;
      case 'overlay':
        kindColor = const Color(0xFFf7d731);
        break;
      default:
        kindColor = const Color(0xFF8b949e);
    }

    // Don't show destroy for the launcher itself.
    final isLauncher = id == 'launcher';

    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 8, vertical: 2),
      padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 6),
      decoration: BoxDecoration(
        color: const Color(0xFF161b22),
        borderRadius: BorderRadius.circular(6),
        border: Border.all(color: const Color(0xFF30363d)),
      ),
      child: Row(
        children: [
          Container(
            width: 5, height: 5,
            decoration: BoxDecoration(
              shape: BoxShape.circle,
              color: kindColor,
            ),
          ),
          const SizedBox(width: 8),
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Text(id,
                        style: const TextStyle(
                            color: Color(0xFFe6edf3),
                            fontSize: 10,
                            fontWeight: FontWeight.w600,
                            fontFamily: 'Consolas')),
                    const SizedBox(width: 8),
                    Container(
                      padding: const EdgeInsets.symmetric(
                          horizontal: 4, vertical: 1),
                      decoration: BoxDecoration(
                        color: kindColor.withOpacity(0.12),
                        borderRadius: BorderRadius.circular(3),
                      ),
                      child: Text(kind.toUpperCase(),
                          style: TextStyle(
                              color: kindColor,
                              fontSize: 7,
                              fontWeight: FontWeight.w700)),
                    ),
                  ],
                ),
                const SizedBox(height: 2),
                Text(
                    'ws=$workspace'
                    '${parent.isNotEmpty ? ' parent=$parent' : ''}'
                    '${followsParent ? ' [follows]' : ''}'
                    '${persistent ? ' [persistent]' : ''}',
                    style: const TextStyle(
                        color: Color(0xFF484f58),
                        fontSize: 8,
                        fontFamily: 'Consolas')),
              ],
            ),
          ),
          if (!isLauncher)
            GestureDetector(
              onTap: () => _destroySurface(id),
              child: Container(
                width: 22, height: 22,
                decoration: BoxDecoration(
                  color: const Color(0xFFf85149).withOpacity(0.1),
                  borderRadius: BorderRadius.circular(4),
                ),
                child: const Icon(Icons.close, size: 12,
                    color: Color(0xFFf85149)),
              ),
            ),
        ],
      ),
    );
  }
}
