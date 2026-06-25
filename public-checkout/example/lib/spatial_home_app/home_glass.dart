import 'package:flutter/material.dart';

import '../morphic_app/morphic_window_shape.dart';

/// SPATIAL HOME — shared light-glass visual grammar.
///
/// The surfaces are SPATIAL (compositor-backed): the panel background is the
/// WINDOW's own acrylic backdrop (real blur of whatever is behind), so the
/// content paints NO veil at all — only the widget tiles floating on the
/// glass. Content-only — no geometry, no window truth.
abstract final class HomeGlass {
  // The translucent veil a surface paints (desktop shows through it).
  // Fully transparent: the spatial host's acrylic IS the surface background.
  static const Color veil = Color(0x00000000);
  // Widget tiles sitting on the veil.
  static const Color tile = Color(0x99FFFFFF);
  static const Color tileStrong = Color(0xCCFFFFFF);
  // Inverted (dark) tile — the media card.
  static const Color tileDark = Color(0xE61C1C20);

  static const Color ink = Color(0xFF2A2A2E); // primary text
  static const Color inkSoft = Color(0xFF6F6F76); // secondary text
  static const Color accent = Color(0xFF34C759); // the iOS-green of toggles

  static const double tileRadius = 18;
}

/// A rounded translucent tile on the glass panel.
class GlassTile extends StatelessWidget {
  final Widget child;
  final EdgeInsetsGeometry padding;
  final Color color;
  const GlassTile({
    super.key,
    required this.child,
    this.padding = const EdgeInsets.all(14),
    this.color = HomeGlass.tile,
  });

  @override
  Widget build(BuildContext context) => Container(
        padding: padding,
        decoration: BoxDecoration(
          color: color,
          borderRadius: BorderRadius.circular(HomeGlass.tileRadius),
        ),
        child: child,
      );
}

/// Small iOS-style switch in the scene's accent green.
class GlassSwitch extends StatelessWidget {
  final bool value;
  final ValueChanged<bool> onChanged;
  const GlassSwitch({super.key, required this.value, required this.onChanged});

  @override
  Widget build(BuildContext context) => Transform.scale(
        scale: 0.78,
        child: Switch(
          value: value,
          onChanged: onChanged,
          activeTrackColor: HomeGlass.accent,
          activeColor: Colors.white,
          inactiveTrackColor: const Color(0x33000000),
          inactiveThumbColor: Colors.white,
          trackOutlineColor: const WidgetStatePropertyAll(Colors.transparent),
        ),
      );
}

/// The transparent MaterialApp wrapper every home surface boots with. The
/// window is alpha-glass: wherever content doesn't paint, the DESKTOP shows —
/// and [MorphicWindowShape] clips to the surface's runtime-owned corner
/// radius, so the window's composited shape is the rounded content itself.
class HomeSurfaceApp extends StatelessWidget {
  final Widget child;
  const HomeSurfaceApp({super.key, required this.child});

  @override
  Widget build(BuildContext context) => MaterialApp(
        debugShowCheckedModeBanner: false,
        theme: ThemeData.light(useMaterial3: true).copyWith(
          scaffoldBackgroundColor: Colors.transparent,
          textTheme: Typography.blackCupertino.apply(
            bodyColor: HomeGlass.ink,
            displayColor: HomeGlass.ink,
          ),
        ),
        home: Scaffold(
          backgroundColor: Colors.transparent,
          body: MorphicWindowShape(child: child),
        ),
      );
}
