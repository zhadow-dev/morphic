import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'plane_dimmable.dart';

/// PHASE 10.2 — Overlay surface entrypoint.
///
/// Runs in its own Flutter engine inside a Morphic surface HWND.
/// Transient semantic utility — command palette / quick search.
/// Non-restorable, lightweight, may not group. Overlay semantics only.
/// Dismissible: sends destroy signal on escape or dismiss action.
@pragma('vm:entry-point')
void overlayMain() {
  runApp(const _OverlayApp());
}

class _OverlayApp extends StatelessWidget {
  const _OverlayApp();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      color: Colors.transparent,  // PHASE 11C — transparent root so the DWM acrylic shows
      theme: ThemeData.dark(useMaterial3: true),
      home: const PlaneDimmable(child: _OverlayContent()),
    );
  }
}

class _OverlayContent extends StatefulWidget {
  const _OverlayContent();

  @override
  State<_OverlayContent> createState() => _OverlayContentState();
}

class _OverlayContentState extends State<_OverlayContent> {
  static const _channel = MethodChannel('morphic');
  final TextEditingController _searchController = TextEditingController();
  final FocusNode _searchFocus = FocusNode();

  static const _commands = [
    _Cmd(Icons.add, 'New Workspace', 'Create a new workspace surface'),
    _Cmd(Icons.palette, 'Open Palette', 'Spawn a tool palette'),
    _Cmd(Icons.info_outline, 'Open Inspector', 'Spawn an inspector panel'),
    _Cmd(Icons.layers, 'Show Topology', 'Display surface topology'),
    _Cmd(Icons.save, 'Save Session', 'Save current session state'),
    _Cmd(Icons.refresh, 'Restore Session', 'Restore last saved session'),
    _Cmd(Icons.bug_report, 'Run Diagnostics', 'Run validation suites'),
    _Cmd(Icons.close, 'Dismiss', 'Close this overlay'),
  ];

  List<_Cmd> _filtered = _commands;

  @override
  void initState() {
    super.initState();
    _searchController.addListener(_onSearch);
    // Auto-focus the search field
    WidgetsBinding.instance.addPostFrameCallback((_) {
      _searchFocus.requestFocus();
    });
  }

  void _onSearch() {
    final q = _searchController.text.toLowerCase();
    setState(() {
      if (q.isEmpty) {
        _filtered = _commands;
      } else {
        _filtered = _commands
            .where((c) =>
                c.label.toLowerCase().contains(q) ||
                c.description.toLowerCase().contains(q))
            .toList();
      }
    });
  }

  void _onCommandTap(_Cmd cmd) {
    if (cmd.label == 'Dismiss') {
      // Signal self-destruction (transient semantics)
      _dismiss();
    }
    // Other commands are placeholders for semantic pressure — they log
    // but don't act. The point is the overlay's behavioral feel, not
    // command execution.
  }

  void _dismiss() {
    // Transient overlay self-destructs. In a real flow this would signal
    // the ecology to destroy this surface. For now it's a behavioral stub.
    debugPrint('[OVERLAY] dismiss requested (transient self-destruct)');
  }

  @override
  void dispose() {
    _searchController.dispose();
    _searchFocus.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.transparent,  // PHASE 11C — let the DWM acrylic show through
      body: KeyboardListener(
        focusNode: FocusNode(),
        onKeyEvent: (event) {
          if (event is KeyDownEvent &&
              event.logicalKey == LogicalKeyboardKey.escape) {
            _dismiss();
          }
        },
        child: Column(
          children: [
            // Search bar
            Container(
              padding: const EdgeInsets.fromLTRB(12, 10, 12, 8),
              decoration: const BoxDecoration(
                border:
                    Border(bottom: BorderSide(color: Color(0xFF30363d))),
              ),
              child: Row(
                children: [
                  const Icon(Icons.search,
                      size: 14, color: Color(0xFFf7d731)),
                  const SizedBox(width: 8),
                  Expanded(
                    child: TextField(
                      controller: _searchController,
                      focusNode: _searchFocus,
                      style: const TextStyle(
                          color: Color(0xFFe6edf3),
                          fontSize: 12,
                          fontFamily: 'Consolas'),
                      decoration: InputDecoration(
                        hintText: 'Type a command...',
                        hintStyle: TextStyle(
                            color: const Color(0xFF6e7681).withOpacity(0.6),
                            fontSize: 12),
                        border: InputBorder.none,
                        isDense: true,
                        contentPadding: EdgeInsets.zero,
                      ),
                      cursorColor: const Color(0xFFf7d731),
                    ),
                  ),
                  Container(
                    padding:
                        const EdgeInsets.symmetric(horizontal: 4, vertical: 1),
                    decoration: BoxDecoration(
                      color: const Color(0xFF30363d),
                      borderRadius: BorderRadius.circular(3),
                    ),
                    child: const Text('ESC',
                        style: TextStyle(
                            color: Color(0xFF8b949e),
                            fontSize: 8,
                            fontWeight: FontWeight.w600)),
                  ),
                ],
              ),
            ),
            // Command list
            Expanded(
              child: ListView.builder(
                padding: const EdgeInsets.symmetric(vertical: 4),
                itemCount: _filtered.length,
                itemBuilder: (context, index) {
                  final cmd = _filtered[index];
                  return _commandRow(cmd, index == 0);
                },
              ),
            ),
            // Footer
            Container(
              height: 22,
              padding: const EdgeInsets.symmetric(horizontal: 10),
              decoration: const BoxDecoration(
                border: Border(top: BorderSide(color: Color(0xFF30363d))),
              ),
              child: Row(
                children: [
                  Container(
                    width: 5, height: 5,
                    decoration: const BoxDecoration(
                      shape: BoxShape.circle,
                      color: Color(0xFFf7d731),
                    ),
                  ),
                  const SizedBox(width: 6),
                  const Text('OVERLAY · TRANSIENT',
                      style: TextStyle(
                          color: Color(0xFF484f58),
                          fontSize: 7,
                          fontWeight: FontWeight.w600,
                          letterSpacing: 0.8)),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _commandRow(_Cmd cmd, bool highlighted) {
    return GestureDetector(
      onTap: () => _onCommandTap(cmd),
      child: Container(
        height: 32,
        margin: const EdgeInsets.symmetric(horizontal: 6, vertical: 1),
        padding: const EdgeInsets.symmetric(horizontal: 10),
        decoration: BoxDecoration(
          color: highlighted
              ? const Color(0xFFf7d731).withOpacity(0.08)
              : Colors.transparent,
          borderRadius: BorderRadius.circular(4),
        ),
        child: Row(
          children: [
            Icon(cmd.icon,
                size: 13,
                color: highlighted
                    ? const Color(0xFFf7d731)
                    : const Color(0xFF6e7681)),
            const SizedBox(width: 10),
            Expanded(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(cmd.label,
                      style: TextStyle(
                          color: highlighted
                              ? const Color(0xFFe6edf3)
                              : const Color(0xFFc9d1d9),
                          fontSize: 11,
                          fontWeight: FontWeight.w500)),
                  Text(cmd.description,
                      style: const TextStyle(
                          color: Color(0xFF484f58), fontSize: 8)),
                ],
              ),
            ),
          ],
        ),
      ),
    );
  }
}

class _Cmd {
  final IconData icon;
  final String label;
  final String description;
  const _Cmd(this.icon, this.label, this.description);
}
