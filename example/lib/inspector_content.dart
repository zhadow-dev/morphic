import 'package:flutter/material.dart';

import 'plane_dimmable.dart';

/// PHASE 10.2 — Inspector surface entrypoint.
///
/// Runs in its own Flutter engine inside a Morphic surface HWND.
/// Contextual information utility tied to a workspace. Shows property
/// list / metadata panel. Feels secondary — muted colors, smaller
/// typography. Tied to parent workspace ownership.
@pragma('vm:entry-point')
void inspectorMain() {
  runApp(const _InspectorApp());
}

class _InspectorApp extends StatelessWidget {
  const _InspectorApp();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true),
      home: const PlaneDimmable(child: _InspectorContent()),
    );
  }
}

class _InspectorContent extends StatelessWidget {
  const _InspectorContent();

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: const Color(0xFF0d1117),
      body: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          // Header
          Container(
            height: 28,
            padding: const EdgeInsets.symmetric(horizontal: 10),
            decoration: const BoxDecoration(
              color: Color(0xFF161b22),
              border: Border(bottom: BorderSide(color: Color(0xFF30363d))),
            ),
            child: Row(
              children: [
                Container(
                  width: 5, height: 5,
                  decoration: const BoxDecoration(
                    shape: BoxShape.circle,
                    color: Color(0xFFd2a8ff),
                  ),
                ),
                const SizedBox(width: 6),
                Text('INSPECTOR',
                    style: TextStyle(
                        color: const Color(0xFFd2a8ff).withOpacity(0.9),
                        fontSize: 8,
                        fontWeight: FontWeight.w700,
                        letterSpacing: 1.2)),
              ],
            ),
          ),
          // Properties list
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(10),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _section('SURFACE'),
                  _prop('Kind', 'Inspector'),
                  _prop('State', 'Active'),
                  _prop('Focusable', 'true'),
                  _prop('Detachable', 'true'),
                  _prop('Groupable', 'true'),
                  _prop('Persistent', 'false'),
                  const SizedBox(height: 12),
                  _section('OWNERSHIP'),
                  _prop('Parent', '(workspace)'),
                  _prop('Follows Parent', 'true'),
                  _prop('Workspace', '(current)'),
                  const SizedBox(height: 12),
                  _section('GEOMETRY'),
                  _prop('Position', '—'),
                  _prop('Size', '—'),
                  _prop('Z-Layer', '—'),
                  const SizedBox(height: 12),
                  _section('RENDERER'),
                  _prop('Type', 'Flutter'),
                  _prop('Engine', 'secondary'),
                  _prop('Activity', 'Active'),
                  const SizedBox(height: 12),
                  _section('POLICY FLAGS'),
                  _flag('participates_in_restore', false),
                  _flag('transient', false),
                  _flag('follows_parent', true),
                  _flag('overlay_semantics', false),
                ],
              ),
            ),
          ),
        ],
      ),
    );
  }

  static Widget _section(String label) {
    return Padding(
      padding: const EdgeInsets.only(bottom: 6),
      child: Text(label,
          style: const TextStyle(
              color: Color(0xFF6e7681),
              fontSize: 8,
              fontWeight: FontWeight.w700,
              letterSpacing: 1)),
    );
  }

  static Widget _prop(String label, String value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          SizedBox(
            width: 100,
            child: Text(label,
                style: const TextStyle(
                    color: Color(0xFF8b949e), fontSize: 10)),
          ),
          Expanded(
            child: Text(value,
                style: const TextStyle(
                    color: Color(0xFFc9d1d9),
                    fontSize: 10,
                    fontFamily: 'Consolas')),
          ),
        ],
      ),
    );
  }

  static Widget _flag(String name, bool value) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: [
          Icon(
            value ? Icons.check_box : Icons.check_box_outline_blank,
            size: 12,
            color: value
                ? const Color(0xFFd2a8ff).withOpacity(0.7)
                : const Color(0xFF30363d),
          ),
          const SizedBox(width: 6),
          Text(name,
              style: TextStyle(
                  color: value
                      ? const Color(0xFFc9d1d9)
                      : const Color(0xFF6e7681),
                  fontSize: 9,
                  fontFamily: 'Consolas')),
        ],
      ),
    );
  }
}
