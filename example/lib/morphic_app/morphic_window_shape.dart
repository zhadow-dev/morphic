import 'package:flutter/material.dart';

import 'package:morphic/morphic.dart';

/// MORPHIC AUTHORING LAYER — SPATIAL CHROME: the window's shape.
///
/// Wrap a surface's root content with this and the window takes whatever corner
/// radius the RUNTIME holds for it (spawned via `SurfaceSpec.cornerRadius`,
/// edited live via [MorphicSurface.setCornerRadius]) — the app never hand-clips.
///
/// Mechanism (the only one the OS allows on this substrate): the surface is a
/// full-glass frame, so unpainted pixels are genuinely transparent to the
/// desktop; clipping the content's alpha therefore IS the window's composited
/// shape — antialiased, any radius from 0 to a full circle. A native window
/// region cannot do this (it doesn't clip DWM-composited output and degrades
/// the glass), and DWM presets only offer ~4/~8 px. Requires the surface to be
/// spawned with a transparent mode and `backdrop: 'none'` (an ACCENT blur
/// backdrop always fills the rectangle — OS boundary).
class MorphicWindowShape extends StatefulWidget {
  final Widget child;
  const MorphicWindowShape({super.key, required this.child});

  @override
  State<MorphicWindowShape> createState() => _MorphicWindowShapeState();
}

class _MorphicWindowShapeState extends State<MorphicWindowShape> {
  int _radiusPx = 0; // native px — converted to logical at build

  @override
  void initState() {
    super.initState();
    // Live updates: the runtime pushes 'surface.chrome' to this engine when
    // setCornerRadius changes the stored value.
    AppBus.on('surface.chrome', (p) {
      final r = p['cornerRadius'];
      if (r is int && mounted) setState(() => _radiusPx = r);
    });
    // Initial value: pull once identity is available (spawn-time radius).
    MorphicSurface.getCornerRadius().then((r) {
      if (mounted && r != _radiusPx) setState(() => _radiusPx = r);
    });
  }

  @override
  Widget build(BuildContext context) {
    if (_radiusPx <= 0) return widget.child;
    final dpr = MediaQuery.maybeDevicePixelRatioOf(context) ??
        View.of(context).devicePixelRatio;
    return ClipRRect(
      borderRadius: BorderRadius.circular(_radiusPx / dpr),
      child: widget.child,
    );
  }
}
