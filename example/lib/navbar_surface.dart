import 'package:flutter/material.dart';

/// PHASE 2 surface entrypoint — runs in its own Flutter engine inside a
/// SurfaceShell. Must be a top-level @pragma('vm:entry-point') function.
@pragma('vm:entry-point')
void navbarMain() {
  runApp(const _NavbarSurfaceApp());
}

class _NavbarSurfaceApp extends StatelessWidget {
  const _NavbarSurfaceApp();

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true),
      home: const Scaffold(
        backgroundColor: Color(0xFF0d1117),
        body: NavbarSurface(),
      ),
    );
  }
}

/// A composition unit that hosts the navigation rail.
///
/// PHASE 1A: a minimal static navbar. Like [EditorSurface] it is a plain widget
/// composed into the single shell tree, NOT yet an independent Morphic surface.
class NavbarSurface extends StatelessWidget {
  const NavbarSurface({super.key});

  @override
  Widget build(BuildContext context) {
    return Container(
      width: 56,
      color: const Color(0xFF161b22),
      child: Column(
        children: const [
          SizedBox(height: 12),
          _NavIcon(icon: Icons.description, selected: true),
          _NavIcon(icon: Icons.folder_open),
          _NavIcon(icon: Icons.search),
          Spacer(),
          _NavIcon(icon: Icons.settings),
          SizedBox(height: 12),
        ],
      ),
    );
  }
}

class _NavIcon extends StatelessWidget {
  final IconData icon;
  final bool selected;
  const _NavIcon({required this.icon, this.selected = false});

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 10),
      child: Icon(
        icon,
        size: 22,
        color: selected ? const Color(0xFF58a6ff) : const Color(0xFF8b949e),
      ),
    );
  }
}
