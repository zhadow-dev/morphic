import 'package:flutter/material.dart';

import '../morphic_app/atmosphere_profile.dart';
import '../morphic_app/morphic_spatial_theme.dart';
import 'notes_session.dart';

/// MORPHIC SECOND APP — Notes Inspector (COMPANION archetype), driven by [NotesSession].
///
/// Shows the ACTIVE document's details + the OPEN-DOCS registry. Holds its own NotesSession replica
/// (built from the AppBus). Passive DATA mirror — never owns input, never decides which doc is active
/// (§4b: the focused editor self-reports); rebinds as focus moves.
///
/// VISUAL: a COHERENT frosted companion surface — ONE window, calm + readable, receding behind the
/// editor's presence. Spatiality is MATERIAL + hierarchy, not shape (PHASE 1 correction: the detached
/// header island + body mass were fake segmentation).
@pragma('vm:entry-point')
void notesInspectorMain() {
  runApp(const _NotesInspectorApp());
}

class _NotesInspectorApp extends StatelessWidget {
  const _NotesInspectorApp();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        scaffoldBackgroundColor: Colors.transparent,
      ),
      home: const MorphicAtmosphere(
          profile: AtmosphereProfile.inspector, child: _NotesInspector()),
    );
  }
}

class _NotesInspector extends StatefulWidget {
  const _NotesInspector();

  @override
  State<_NotesInspector> createState() => _NotesInspectorState();
}

class _NotesInspectorState extends State<_NotesInspector> {
  late final NotesSession _session;

  @override
  void initState() {
    super.initState();
    _session = NotesSession();
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

  @override
  Widget build(BuildContext context) {
    final a = _session.activeDoc;
    final title = (a['title'] as String?) ?? '';
    final words = a['words'] ?? 0;
    final outline = a['outline'] is List
        ? (a['outline'] as List).map((e) => e.toString()).toList()
        : const <String>[];

    return Scaffold(
      // Field comes from the AtmosphereProfile (applied by MorphicAtmosphere); Scaffold stays clear.
      backgroundColor: Colors.transparent,
      body: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Soft top region — implied frame/drag, no hard border (dissolved chrome, not removed).
          Padding(
            padding: const EdgeInsets.fromLTRB(16, 14, 14, 8),
            child: Row(
              children: [
                const Icon(Icons.info_outline, size: 14, color: MorphicColors.muted),
                const SizedBox(width: 8),
                const Text('INSPECTOR', style: MorphicType.label),
                const Spacer(),
                Text('${_session.docs.length} docs', style: MorphicType.whisper),
              ],
            ),
          ),
          Expanded(
            child: !_session.hasActive
                ? const Center(
                    child: Text('No active note',
                        style: TextStyle(color: MorphicColors.whisper, fontSize: 11)))
                : ListView(
                    padding: const EdgeInsets.fromLTRB(16, 8, 16, 14),
                    children: [
                      _field('ACTIVE DOC', _session.activeDocId ?? '—'),
                      _field('TITLE', title.isEmpty ? '—' : title),
                      _field('WORDS', '$words'),
                      const SizedBox(height: 14),
                      _label('OUTLINE'),
                      const SizedBox(height: 6),
                      if (outline.isEmpty)
                        const Text('(no headings)',
                            style: TextStyle(color: MorphicColors.whisper, fontSize: 11))
                      else
                        ...outline.map((h) => Padding(
                              padding: const EdgeInsets.symmetric(vertical: 2),
                              child: Text(h,
                                  style: const TextStyle(
                                      color: MorphicColors.body,
                                      fontSize: 12,
                                      fontFamily: 'Consolas')),
                            )),
                      const SizedBox(height: 18),
                      _label('OPEN DOCS  ·  ${_session.docs.length}'),
                      const SizedBox(height: 6),
                      ..._session.docs.entries.map((e) {
                        final active = e.key == _session.activeDocId;
                        return Padding(
                          padding: const EdgeInsets.symmetric(vertical: 3),
                          child: Row(
                            children: [
                              Container(
                                width: 5,
                                height: 5,
                                decoration: BoxDecoration(
                                  shape: BoxShape.circle,
                                  color: active ? MorphicColors.accent : const Color(0xFF2D343D),
                                ),
                              ),
                              const SizedBox(width: 8),
                              Expanded(
                                child: Text(
                                  e.value.isEmpty ? '(untitled)' : e.value,
                                  overflow: TextOverflow.ellipsis,
                                  style: TextStyle(
                                      color: active ? MorphicColors.ink : const Color(0xFF8B949E),
                                      fontSize: 12,
                                      fontWeight: active ? FontWeight.w600 : FontWeight.w400),
                                ),
                              ),
                            ],
                          ),
                        );
                      }),
                    ],
                  ),
          ),
        ],
      ),
    );
  }

  Widget _label(String text) => Text(text,
      style: const TextStyle(
          color: MorphicColors.muted,
          fontSize: 9,
          fontWeight: FontWeight.w700,
          letterSpacing: 1.2));

  Widget _field(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 5),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          _label(label),
          const SizedBox(height: 2),
          Text(value, style: const TextStyle(color: MorphicColors.ink, fontSize: 13)),
        ],
      ),
    );
  }
}
