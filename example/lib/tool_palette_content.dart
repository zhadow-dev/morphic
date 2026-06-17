import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'plane_dimmable.dart';

/// PHASE 10.2 — ToolPalette surface entrypoint.
///
/// Runs in its own Flutter engine inside a Morphic surface HWND.
/// Floating utility/helper surface — formatting palette, symbol browser,
/// quick actions. Lightweight, transient feel. Non-root, follows workspace
/// ownership.
@pragma('vm:entry-point')
void toolPaletteMain() {
  runApp(const _ToolPaletteApp());
}

class _ToolPaletteApp extends StatelessWidget {
  const _ToolPaletteApp();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true),
      home: const PlaneDimmable(child: _ToolPaletteContent()),
    );
  }
}

class _ToolPaletteContent extends StatefulWidget {
  const _ToolPaletteContent();

  @override
  State<_ToolPaletteContent> createState() => _ToolPaletteContentState();
}

class _ToolPaletteContentState extends State<_ToolPaletteContent> {
  final Set<int> _activeTools = {};

  // SPATIAL MIGRATION (visual mode) — C++ pushes setGlass(true/false) on 'morphic/plane_material'
  // from this surface's native appearance: true when the palette's window is glass (it adopted a
  // GLASS plane's material via ReconcilePlaneMaterial), false when STANDARD (the default, incl.
  // membership in a grounded/opaque plane). Frosted-translucent when glass so it joins the
  // plane's visual field; solid opaque otherwise. VISUAL ONLY. Separate channel from
  // PlaneDimmable's 'morphic/plane' (one handler per channel).
  static const _materialChannel = MethodChannel('morphic/plane_material');
  String _material = 'standard';
  bool get _glass => _material != 'standard';

  @override
  void initState() {
    super.initState();
    _materialChannel.setMethodCallHandler((call) async {
      if (call.method == 'setMaterial') {
        final next = call.arguments is String ? call.arguments as String : 'standard';
        if (next != _material && mounted) setState(() => _material = next);
      }
      return null;
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      // Frosted translucent when the window is glass (native glass behind shows the blur);
      // solid opaque otherwise (Standard).
      backgroundColor: _glass ? const Color(0x73161b22) : const Color(0xFF161b22),
      body: Column(
        children: [
          // Header
          Container(
            height: 28,
            padding: const EdgeInsets.symmetric(horizontal: 10),
            decoration: BoxDecoration(
              color: _glass ? const Color(0xCC1c2128) : const Color(0xFF1c2128),
              border: const Border(bottom: BorderSide(color: Color(0xFF30363d))),
            ),
            child: Row(
              children: [
                Container(
                  width: 5, height: 5,
                  decoration: const BoxDecoration(
                    shape: BoxShape.circle,
                    color: Color(0xFFf0883e),
                  ),
                ),
                const SizedBox(width: 6),
                Text('TOOLS',
                    style: TextStyle(
                        color: const Color(0xFFf0883e).withOpacity(0.9),
                        fontSize: 8,
                        fontWeight: FontWeight.w700,
                        letterSpacing: 1.2)),
              ],
            ),
          ),
          // Tool grid
          Expanded(
            child: Padding(
              padding: const EdgeInsets.all(8),
              child: GridView.count(
                crossAxisCount: 3,
                mainAxisSpacing: 4,
                crossAxisSpacing: 4,
                children: [
                  _tool(0, Icons.format_bold, 'Bold'),
                  _tool(1, Icons.format_italic, 'Italic'),
                  _tool(2, Icons.format_underlined, 'Under'),
                  _tool(3, Icons.format_list_bulleted, 'List'),
                  _tool(4, Icons.code, 'Code'),
                  _tool(5, Icons.link, 'Link'),
                  _tool(6, Icons.format_quote, 'Quote'),
                  _tool(7, Icons.table_chart_outlined, 'Table'),
                  _tool(8, Icons.image_outlined, 'Image'),
                ],
              ),
            ),
          ),
          // Quick actions row
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 6),
            decoration: const BoxDecoration(
              border: Border(top: BorderSide(color: Color(0xFF30363d))),
            ),
            child: Row(
              children: [
                _quickAction(Icons.undo, 'Undo'),
                const SizedBox(width: 4),
                _quickAction(Icons.redo, 'Redo'),
                const Spacer(),
                _quickAction(Icons.content_copy, 'Copy'),
                const SizedBox(width: 4),
                _quickAction(Icons.content_paste, 'Paste'),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _tool(int index, IconData icon, String label) {
    final active = _activeTools.contains(index);
    return GestureDetector(
      onTap: () => setState(() {
        if (active) {
          _activeTools.remove(index);
        } else {
          _activeTools.add(index);
        }
      }),
      child: Container(
        decoration: BoxDecoration(
          color: active
              ? const Color(0xFFf0883e).withOpacity(0.15)
              : const Color(0xFF0d1117),
          borderRadius: BorderRadius.circular(6),
          border: Border.all(
            color: active
                ? const Color(0xFFf0883e).withOpacity(0.4)
                : const Color(0xFF30363d),
          ),
        ),
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(icon,
                size: 16,
                color: active
                    ? const Color(0xFFf0883e)
                    : const Color(0xFF8b949e)),
            const SizedBox(height: 2),
            Text(label,
                style: TextStyle(
                    color: active
                        ? const Color(0xFFf0883e)
                        : const Color(0xFF6e7681),
                    fontSize: 7,
                    fontWeight: FontWeight.w500)),
          ],
        ),
      ),
    );
  }

  Widget _quickAction(IconData icon, String tooltip) {
    return Tooltip(
      message: tooltip,
      child: Container(
        width: 28, height: 24,
        decoration: BoxDecoration(
          color: const Color(0xFF0d1117),
          borderRadius: BorderRadius.circular(4),
          border: Border.all(color: const Color(0xFF30363d)),
        ),
        child: Icon(icon, size: 13, color: const Color(0xFF8b949e)),
      ),
    );
  }
}
