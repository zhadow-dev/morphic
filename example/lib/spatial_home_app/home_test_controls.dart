import 'dart:async';

import 'package:flutter/material.dart';

import 'package:morphic/morphic.dart';
import '../forensic.dart';
import 'home_glass.dart';

/// Entry-point for surfaces spawned at runtime by the "New surface" button.
/// Each gets its own engine + the standard diagnostic content.
@pragma('vm:entry-point')
void homeSpawnedMain() =>
    runApp(const HomeSurfaceApp(child: DiagnosticSurface(label: 'spawned')));

// DEBUG — verify the close-crash fix headless: one surface closes itself so we
// can confirm no infinite-Drop crash + siblings survive, without a human click.
// Set false to disable.
const bool kDebugAutoCloseProbe = false;

/// SPATIAL HOME — MINIMAL INTERACTION + REDRAW DIAGNOSTIC.
///
/// Strips a surface to the essentials that answer two questions live:
///
///  · Does input reach this spatial surface? Tap **+1** — if the count rises,
///    pointer input is being routed into the parked engine AND the resulting
///    repaint is re-captured by the compositor.
///  · Is the surface being re-rendered at all? The main panel runs a continuous
///    **animation** — if it keeps moving, the compositor is re-capturing Flutter
///    frames; if it's frozen, redraw/capture is the problem, independent of input.
///
/// A faint fill makes each surface's bounds + hit area visible.
class DiagnosticSurface extends StatefulWidget {
  final String label;

  /// Stack the controls vertically (the tall, narrow sidebar; the circle).
  final bool vertical;

  /// Run the redraw animation + use the roomy, labelled layout (the main panel).
  final bool roomy;

  const DiagnosticSurface({
    super.key,
    required this.label,
    this.vertical = false,
    this.roomy = false,
  });

  @override
  State<DiagnosticSurface> createState() => _DiagnosticSurfaceState();
}

class _DiagnosticSurfaceState extends State<DiagnosticSurface>
    with SingleTickerProviderStateMixin {
  static const Color _ink = Color(0xFF1C1C20);
  static const Color _soft = Color(0xFF6F6F76);

  int _count = 0;
  int _frames = 0;
  int _pointers = 0;
  AnimationController? _spin;

  @override
  void initState() {
    super.initState();
    Forensic.attach(); // this surface runs in its own engine/isolate
    Forensic.log('FLUTTER-BOOT', '${widget.label} surface engine started');

    // DEBUG probe: the circle auto-closes itself after a few seconds so the
    // close-teardown path runs headless (no human click available here).
    if (kDebugAutoCloseProbe && widget.label == 'circle') {
      Timer(const Duration(seconds: 6), () {
        Forensic.log('FLUTTER-INPUT', 'circle AUTO-CLOSE probe firing');
        MorphicSurface.close();
      });
    }
    if (widget.roomy) {
      _spin = AnimationController(
        vsync: this,
        duration: const Duration(seconds: 3),
      )..repeat();

      // LAYER 1 — Flutter frame PRODUCTION. Fires once per rasterised frame.
      // The repeating animation keeps requesting frames, so while this surface
      // is parked offscreen: if the count keeps climbing, Flutter is NOT
      // throttling the hidden engine (a frozen visual is then DWM/WGC capture
      // starvation downstream); if it stalls, Flutter itself slept the engine.
      WidgetsBinding.instance.addPersistentFrameCallback((_) {
        _frames++;
        if (_frames % 60 == 0) {
          Forensic.log('FLUTTER-LIVE', '${widget.label} frames=$_frames');
        }
      });
    }
  }

  static int _spawnSeq = 0;

  // Spawn a brand-new spatial surface at runtime (its own Flutter engine),
  // owned by this workspace, via the ecology channel. Staggered so they don't
  // stack. Proves create + the engine-reap path (close frees its memory).
  Future<void> _spawnNew() async {
    final parentId = await MorphicSurface.currentId();
    final n = ++_spawnSeq;
    await EcologyController().spawnSurface(
      kind: 'tool_palette',
      parentId: parentId,
      entrypoint: 'homeSpawnedMain',
      x: 360 + (n % 6) * 70,
      y: 240 + (n % 6) * 70,
      width: 260,
      height: 200,
      backend: 'spatial',
      shape: 'rounded',
      material: 'acrylic',
      transparency: 'full_glass',
      backdrop: 'none',
      chromeless: true,
      cornerRadius: 22,
      materialTint: 0x59FFFFFF,
      elevation: 20,
      composed: true,
    );
  }

  // LAYER 4 — does input actually REACH Flutter? Logged independently of any
  // visual result, so we can tell "input dead" apart from "visual frozen".
  void _onPointer(PointerDownEvent e) {
    _pointers++;
    Forensic.log(
        'FLUTTER-INPUT',
        '${widget.label} pointerDown #$_pointers '
            '@${e.localPosition.dx.toStringAsFixed(0)},'
            '${e.localPosition.dy.toStringAsFixed(0)}');
  }

  @override
  void dispose() {
    _spin?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final dense = !widget.roomy;
    final gap = SizedBox(width: dense ? 10 : 16, height: dense ? 10 : 16);

    final parts = <Widget>[
      if (_spin != null) ...[_spinner(), gap],
      Text(widget.label,
          style: const TextStyle(
              fontSize: 11, fontWeight: FontWeight.w600, color: _soft)),
      Text('$_count',
          style: TextStyle(
              fontSize: widget.roomy ? 56 : 26,
              fontWeight: FontWeight.w900,
              height: 1.0,
              color: _ink)),
      gap,
      if (widget.roomy) ...[
        _Btn(
            icon: Icons.add_box_outlined,
            label: 'New surface',
            bg: const Color(0xFF34C759),
            onTap: _spawnNew),
        gap,
      ],
      _Btn(
          icon: Icons.add,
          label: dense ? null : '+1',
          bg: const Color(0xFF0A84FF),
          onTap: () => setState(() => _count++)),
      gap,
      _Btn(
          icon: Icons.power_settings_new,
          label: dense ? null : 'Close',
          bg: const Color(0xFFFF3B30),
          onTap: () => MorphicSurface.close()),
    ];

    final core = widget.vertical
        ? Column(mainAxisSize: MainAxisSize.min, children: parts)
        : Row(mainAxisSize: MainAxisSize.min, children: parts);

    return Listener(
      behavior: HitTestBehavior.translucent,
      onPointerDown: _onPointer,
      child: Container(
        color: const Color(0x14000000),
        alignment: Alignment.center,
        padding: const EdgeInsets.all(8),
        child: core,
      ),
    );
  }

  Widget _spinner() => RotationTransition(
        turns: _spin!,
        child: Container(
          width: 72,
          height: 72,
          decoration: BoxDecoration(
            borderRadius: BorderRadius.circular(14),
            gradient: const LinearGradient(
                colors: [Color(0xFF34C759), Color(0xFF0A84FF)]),
          ),
          child: const Icon(Icons.sync, color: Colors.white, size: 32),
        ),
      );
}

/// A solid, obviously-tappable button. Icon-only when [label] is null (dense
/// surfaces); icon + label otherwise.
class _Btn extends StatelessWidget {
  final IconData icon;
  final String? label;
  final Color bg;
  final VoidCallback onTap;
  const _Btn(
      {required this.icon,
      required this.label,
      required this.bg,
      required this.onTap});

  @override
  Widget build(BuildContext context) {
    return Material(
      color: Colors.transparent,
      child: InkWell(
        onTap: onTap,
        borderRadius: BorderRadius.circular(label == null ? 19 : 16),
        child: label == null
            ? Container(
                width: 38,
                height: 38,
                decoration: BoxDecoration(color: bg, shape: BoxShape.circle),
                child: Icon(icon, size: 19, color: Colors.white),
              )
            : Container(
                padding:
                    const EdgeInsets.symmetric(horizontal: 16, vertical: 11),
                decoration: BoxDecoration(
                    color: bg, borderRadius: BorderRadius.circular(16)),
                child: Row(mainAxisSize: MainAxisSize.min, children: [
                  Icon(icon, size: 18, color: Colors.white),
                  const SizedBox(width: 6),
                  Text(label!,
                      style: const TextStyle(
                          color: Colors.white,
                          fontSize: 14,
                          fontWeight: FontWeight.w700)),
                ]),
              ),
      ),
    );
  }
}
