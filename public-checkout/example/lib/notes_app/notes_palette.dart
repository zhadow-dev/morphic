import 'package:flutter/material.dart';

import '../morphic_app/atmosphere_profile.dart';
import '../morphic_app/morphic_spatial_theme.dart';
import 'app_bus.dart';
import 'notes_session.dart';

/// MORPHIC SECOND APP — Notes formatting palette (UTILITY archetype).
///
/// A transient instrument composed under the editor (shared plane). Own engine — can't touch the
/// editor's text; broadcasts a formatting COMMAND over the AppBus (`notes.cmd {docId, action}`, §4b)
/// and the editor owning the ACTIVE doc applies it.
///
/// VISUAL: a COHERENT, ghosted, frosted surface — ONE window, not islands. (PHASE 1 correction: the
/// detached chip + floating rail were fake spatiality / "a window pretending not to be a window".)
/// Utility energy comes from MATERIAL (lightest/most translucent) + airy borderless layout, NOT shape.
@pragma('vm:entry-point')
void notesPaletteMain() => runApp(const _NotesPaletteApp());

class _NotesPaletteApp extends StatelessWidget {
  const _NotesPaletteApp();
  @override
  Widget build(BuildContext context) => MaterialApp(
        debugShowCheckedModeBanner: false,
        theme: ThemeData.dark(useMaterial3: true).copyWith(
          scaffoldBackgroundColor: Colors.transparent,
        ),
        home: const MorphicAtmosphere(
            profile: AtmosphereProfile.utility, child: _NotesPalette()),
      );
}

class _NotesPalette extends StatefulWidget {
  const _NotesPalette();
  @override
  State<_NotesPalette> createState() => _NotesPaletteState();
}

class _NotesPaletteState extends State<_NotesPalette> {
  final _session = NotesSession();

  @override
  void initState() {
    super.initState();
    _session.addListener(_onSession);
  }

  void _onSession() {
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    _session.removeListener(_onSession);
    _session.dispose();
    super.dispose();
  }

  void _send(String action) {
    final doc = _session.activeDocId;
    if (doc == null) return; // nothing active to format
    AppBus.broadcast('notes.cmd', {'docId': doc, 'action': action});
  }

  @override
  Widget build(BuildContext context) {
    final activeTitle = _session.activeDoc['title'] as String?;
    final hasTarget = _session.activeDocId != null;
    return Scaffold(
      // Field comes from the AtmosphereProfile (applied by MorphicAtmosphere); Scaffold stays clear.
      backgroundColor: Colors.transparent,
      body: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Soft top region — a whisper of context. No hard border; this also IS the implied drag
          // region (frame stays readable; chrome is dissolved, not removed).
          Padding(
            padding: const EdgeInsets.fromLTRB(MorphicSpace.gap, 14, MorphicSpace.tight, 10),
            child: Row(
              children: [
                const Icon(Icons.bolt, size: 12, color: MorphicColors.whisper),
                const SizedBox(width: 7),
                Expanded(
                  child: Text(
                    hasTarget
                        ? (activeTitle?.isNotEmpty == true ? activeTitle! : 'active note')
                        : 'no active note',
                    overflow: TextOverflow.ellipsis,
                    style: MorphicType.whisper,
                  ),
                ),
              ],
            ),
          ),
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.only(bottom: 12),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  _group('headings'),
                  _cmd('H1', Icons.title, 'h1', hasTarget),
                  _cmd('H2', Icons.title, 'h2', hasTarget),
                  _cmd('H3', Icons.title, 'h3', hasTarget),
                  _group('lists'),
                  _cmd('Bullet', Icons.format_list_bulleted, 'bullet', hasTarget),
                  _cmd('Checkbox', Icons.check_box_outlined, 'checkbox', hasTarget),
                  _group('inline'),
                  _cmd('Bold', Icons.format_bold, 'bold', hasTarget),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  Widget _group(String label) => Padding(
        padding: const EdgeInsets.fromLTRB(MorphicSpace.gap, 14, MorphicSpace.gap, 5),
        child: Text(label.toUpperCase(),
            style: const TextStyle(
                color: MorphicColors.whisper,
                fontSize: 8,
                fontWeight: FontWeight.w700,
                letterSpacing: 1.6)),
      );

  // Borderless command row — icon + label, airy. No box (utility = material + layout, not chrome).
  Widget _cmd(String label, IconData icon, String action, bool enabled) {
    final color = enabled ? MorphicColors.body : const Color(0xFF3A424C);
    return _Hover(
      onTap: enabled ? () => _send(action) : null,
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: MorphicSpace.gap, vertical: 8),
        child: Row(
          children: [
            Icon(icon, size: 15, color: color),
            const SizedBox(width: 11),
            Expanded(
              child: Text(label,
                  overflow: TextOverflow.ellipsis,
                  style: TextStyle(color: color, fontSize: 12, fontWeight: FontWeight.w400)),
            ),
          ],
        ),
      ),
    );
  }
}

/// A borderless row that lifts faintly on hover (no box, no border).
class _Hover extends StatefulWidget {
  final Widget child;
  final VoidCallback? onTap;
  const _Hover({required this.child, this.onTap});
  @override
  State<_Hover> createState() => _HoverState();
}

class _HoverState extends State<_Hover> {
  bool _hover = false;
  @override
  Widget build(BuildContext context) {
    final interactive = widget.onTap != null;
    return MouseRegion(
      cursor: interactive ? SystemMouseCursors.click : MouseCursor.defer,
      onEnter: (_) => setState(() => _hover = true),
      onExit: (_) => setState(() => _hover = false),
      child: GestureDetector(
        onTap: widget.onTap,
        child: Container(
          color: (_hover && interactive) ? const Color(0x12FFFFFF) : Colors.transparent,
          child: widget.child,
        ),
      ),
    );
  }
}
