import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'package:morphic/morphic.dart';
import 'notes_session.dart';

/// MORPHIC SECOND APP (M2.2E) — Command Palette.
///
/// A TRANSIENT overlay surface (its OWN engine — deliberately NOT embedded in the editor, so the
/// real cross-surface problems actually surface). Opened with Ctrl+K. Lists open docs; ↑↓ to
/// navigate, ↵ to switch to that doc's editor, Esc to dismiss. It closes by destroying its own
/// surface.
///
/// THE §4b TEST: it foregrounds ITSELF on open (so it owns the keyboard while alive) and, on
/// switch, asks the runtime to foreground the TARGET editor — both via the generic, user-driven
/// `surface.activate`. It never routes keys itself; the OS foreground does. Everything it touches
/// is opaque surface ids (from NotesSession's surfaceId↔docId map). This is where transient
/// lifetime, focus return, and keyboard routing get pressure-tested — friction logged, not hidden.
@pragma('vm:entry-point')
void commandPaletteMain() => runApp(const _CommandPaletteApp());

class _CommandPaletteApp extends StatelessWidget {
  const _CommandPaletteApp();
  @override
  Widget build(BuildContext context) => const MaterialApp(
        debugShowCheckedModeBanner: false,
        home: _CommandPalette(),
      );
}

class _CommandPalette extends StatefulWidget {
  const _CommandPalette();
  @override
  State<_CommandPalette> createState() => _CommandPaletteState();
}

class _CommandPaletteState extends State<_CommandPalette> {
  final _ecology = EcologyController();
  final _session = NotesSession();
  String? _mySurfaceId;
  int _selected = 0;

  @override
  void initState() {
    super.initState();
    _session.addListener(_onSession);
    MorphicSurface.currentId().then((id) {
      _mySurfaceId = id;
      // Bring myself to the foreground so I own the keyboard while open (user pressed Ctrl+K).
      _ecology.activateSurface(id);
    });
  }

  void _onSession() {
    if (!mounted) return;
    setState(() {
      final n = _session.docs.length;
      if (_selected >= n) _selected = n == 0 ? 0 : n - 1;
      if (_selected < 0) _selected = 0;
    });
  }

  @override
  void dispose() {
    _session.removeListener(_onSession);
    _session.dispose();
    super.dispose();
  }

  List<MapEntry<String, String>> get _docs => _session.docs.entries.toList();

  void _close() {
    final id = _mySurfaceId;
    if (id != null) _ecology.destroySurface(id);
  }

  void _switchToSelected() {
    final docs = _docs;
    final me = _mySurfaceId;
    if (me != null && _selected >= 0 && _selected < docs.length) {
      final target = _session.surfaceForDoc(docs[_selected].key);
      if (target != null) {
        // M2.2E.1 — ORDERED handoff: foreground the target THEN destroy this palette in one
        // runtime op, so the OS never restores the previously-focused surface (the s1->s2 bug).
        _ecology.handoffActivation(target: target, closeId: me);
        return;
      }
    }
    _close(); // no resolvable target — just dismiss
  }

  KeyEventResult _onKey(FocusNode node, KeyEvent event) {
    if (event is! KeyDownEvent) return KeyEventResult.ignored;
    final n = _docs.length;
    final k = event.logicalKey;
    if (k == LogicalKeyboardKey.escape) {
      _close();
      return KeyEventResult.handled;
    }
    if (k == LogicalKeyboardKey.enter) {
      _switchToSelected();
      return KeyEventResult.handled;
    }
    if (k == LogicalKeyboardKey.arrowDown) {
      if (n > 0) setState(() => _selected = (_selected + 1) % n);
      return KeyEventResult.handled;
    }
    if (k == LogicalKeyboardKey.arrowUp) {
      if (n > 0) setState(() => _selected = (_selected - 1 + n) % n);
      return KeyEventResult.handled;
    }
    return KeyEventResult.ignored;
  }

  @override
  Widget build(BuildContext context) {
    final docs = _docs;
    return Scaffold(
      backgroundColor: const Color(0xFF0D1117),
      body: Focus(
        autofocus: true,
        onKeyEvent: _onKey,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Container(
              padding: const EdgeInsets.fromLTRB(16, 12, 16, 10),
              decoration: const BoxDecoration(
                border: Border(bottom: BorderSide(color: Color(0xFF232A33))),
              ),
              child: Row(
                children: const [
                  Icon(Icons.bolt, size: 15, color: Color(0xFFF7D731)),
                  SizedBox(width: 8),
                  Text('SWITCH DOCUMENT',
                      style: TextStyle(
                          color: Color(0xFFE6EDF3),
                          fontSize: 11,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 1.4)),
                ],
              ),
            ),
            Expanded(
              child: docs.isEmpty
                  ? const Center(
                      child: Text('No open documents',
                          style: TextStyle(color: Color(0xFF49525C), fontSize: 12)))
                  : ListView.builder(
                      padding: const EdgeInsets.symmetric(vertical: 6),
                      itemCount: docs.length,
                      itemBuilder: (context, i) {
                        final selected = i == _selected;
                        return GestureDetector(
                          onTap: () {
                            setState(() => _selected = i);
                            _switchToSelected();
                          },
                          child: Container(
                            color: selected
                                ? const Color(0xFF1F2733)
                                : Colors.transparent,
                            padding: const EdgeInsets.symmetric(
                                horizontal: 16, vertical: 9),
                            child: Row(
                              children: [
                                Icon(Icons.description_outlined,
                                    size: 14,
                                    color: selected
                                        ? const Color(0xFF58A6FF)
                                        : const Color(0xFF49525C)),
                                const SizedBox(width: 10),
                                Expanded(
                                  child: Text(
                                    docs[i].value.isEmpty
                                        ? '(untitled)'
                                        : docs[i].value,
                                    overflow: TextOverflow.ellipsis,
                                    style: TextStyle(
                                        color: selected
                                            ? const Color(0xFFE6EDF3)
                                            : const Color(0xFF8B949E),
                                        fontSize: 13,
                                        fontWeight: selected
                                            ? FontWeight.w600
                                            : FontWeight.w400),
                                  ),
                                ),
                              ],
                            ),
                          ),
                        );
                      },
                    ),
            ),
            Container(
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              decoration: const BoxDecoration(
                border: Border(top: BorderSide(color: Color(0xFF232A33))),
              ),
              child: const Text('↑↓ navigate   ·   ↵ switch   ·   esc close',
                  style: TextStyle(color: Color(0xFF6E7A8A), fontSize: 10)),
            ),
          ],
        ),
      ),
    );
  }
}
