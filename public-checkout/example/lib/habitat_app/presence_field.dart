import 'dart:async';
import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../morphic_app/atmosphere_profile.dart';
import 'package:morphic/morphic.dart';

/// MORPHIC SHOWCASE — The Habitat · scene 01 · PRESENCE FIELD (the anchor).
///
/// NOT a chat window. A persistent, ambient cognitive PRESENCE: a slow breathing luminous core
/// (alive BEFORE you touch it) + a single present-tense line of awareness that quietly evolves —
/// never a log, never an input box, NO primary task. The whole point is a perceptual ontology shift:
/// a viewer should read "this is alive / this isn't an app", not "assistant window".
///
/// This holds the project's FIRST deliberate ambient MOTION — justified because presence cannot be
/// static (a motionless presence field is just wallpaper). Kept whisper-slow (breath ~9s, awareness
/// ~14s), purely content-level: it animates pixels inside this surface and NEVER touches the
/// projection path, window geometry, or any runtime — exactly what the no-animation restraint guarded.
@pragma('vm:entry-point')
void presenceFieldMain() => runApp(const _PresenceApp());

class _PresenceApp extends StatelessWidget {
  const _PresenceApp();
  @override
  Widget build(BuildContext context) => MaterialApp(
        debugShowCheckedModeBanner: false,
        theme: ThemeData.dark(useMaterial3: true).copyWith(
          scaffoldBackgroundColor: Colors.transparent,
        ),
        home: const MorphicAtmosphere(
            profile: AtmosphereProfile.presence, child: _PresenceField()),
      );
}

class _PresenceField extends StatefulWidget {
  const _PresenceField();
  @override
  State<_PresenceField> createState() => _PresenceFieldState();
}

class _PresenceFieldState extends State<_PresenceField>
    with SingleTickerProviderStateMixin {
  late final AnimationController _breath;
  Timer? _awareness;
  int _phrase = 0;
  bool _immersive = false; // M2.8.3 — camera distance: false = authored, true = immersive (zoomed in)

  // Present-tense ambient awareness — ONE drifting thought at a time, never a list. Idle, alive,
  // non-task. Daypart-aware so it reads as environmentally present rather than canned.
  List<String> get _phrases {
    final h = DateTime.now().hour;
    if (h < 5) {
      return const ['the night is still', 'a quiet, even hum', 'nothing is asking for you', 'holding the dark loosely'];
    } else if (h < 12) {
      return const ['the morning is open', 'a few threads, just waking', 'unhurried', 'the light is arriving'];
    } else if (h < 17) {
      return const ['the afternoon is quiet', 'three things, held loosely', 'nothing urgent — just here', 'the light is changing'];
    } else if (h < 21) {
      return const ['the evening settles', 'winding down, slowly', 'a softer attention now', 'the day is loosening'];
    }
    return const ['late, and calm', 'the room is dim', 'no edges right now', 'drifting'];
  }

  @override
  void initState() {
    super.initState();
    _breath = AnimationController(vsync: this, duration: const Duration(seconds: 9))
      ..repeat(reverse: true);
    // The awareness line evolves on its own slow clock — presence, not conversation.
    _awareness = Timer.periodic(const Duration(seconds: 14), (_) {
      if (mounted) setState(() => _phrase = _phrase + 1);
    });
    // M2.8.3 — open the scene with a CAMERA MOVE (not maximize). The habitat spawns at AUTHORED scale
    // (relationships legible); once the companions have composed we zoom the WHOLE composition toward
    // filling the field — anchor + companions scale together from the authored snapshot, so it reads as
    // "the monitor moved closer", never "a window maximized with popups". Two attempts: the first that
    // finds the plane assembled captures the snapshot + zooms; the later one is an idempotent re-apply
    // (reprojects from the same snapshot — no drift). Tap the field to toggle the camera distance.
    WidgetsBinding.instance.addPostFrameCallback((_) {
      for (final ms in const [700, 1500]) {
        Future.delayed(Duration(milliseconds: ms), () {
          if (!mounted) return;
          _immersive = true;
          MorphicSurface.zoomScene(100); // large value clamps to "fill the field" = immersive
        });
      }
    });
  }

  // Tapping the field changes CAMERA DISTANCE (authored <-> immersive) — a soft, taskless interaction,
  // the spatial-scene replacement for maximize/restore. The whole composition scales together.
  void _toggleZoom() {
    _immersive = !_immersive;
    if (_immersive) {
      MorphicSurface.zoomScene(100);
    } else {
      MorphicSurface.resetSceneZoom();
    }
  }

  @override
  void dispose() {
    _breath.dispose();
    _awareness?.cancel();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final phrases = _phrases;
    return Scaffold(
      backgroundColor: Colors.transparent,
      body: GestureDetector(
        behavior: HitTestBehavior.translucent,
        onTap: _toggleZoom, // tap the field to change camera distance (authored <-> immersive)
        child: Stack(
          fit: StackFit.expand,
          children: [
          // The breathing luminous CORE — alive before interaction. Whisper-slow, never a spotlight.
          RepaintBoundary(
            child: AnimatedBuilder(
              animation: _breath,
              builder: (context, _) => CustomPaint(
                painter: _CorePainter(Curves.easeInOut.transform(_breath.value)),
              ),
            ),
          ),
          // A single present-tense line of awareness — low, soft, drifting on its own clock.
          Align(
            alignment: const Alignment(-0.52, 0.30),
            child: AnimatedSwitcher(
              duration: const Duration(milliseconds: 1600),
              switchInCurve: Curves.easeIn,
              switchOutCurve: Curves.easeOut,
              child: Text(
                phrases[_phrase % phrases.length],
                key: ValueKey(_phrase % phrases.length),
                style: const TextStyle(
                  color: Color(0xFFAFC0DE),
                  fontSize: 23,
                  fontWeight: FontWeight.w300,
                  letterSpacing: 0.4,
                  height: 1.4,
                ),
              ),
            ),
          ),
          // A faint signature of presence — not a title, not chrome. Just "here".
          const Align(
            alignment: Alignment(-0.52, 0.46),
            child: Text(
              '· present',
              style: TextStyle(
                color: Color(0xFF4A5468),
                fontSize: 11,
                letterSpacing: 2.6,
                fontWeight: FontWeight.w500,
              ),
            ),
          ),
        ],
        ),
      ),
    );
  }
}

/// The breathing core: a soft off-centre radial glow that slowly swells + brightens. No edges, no
/// spotlight — environmental light, the visual heartbeat of the presence.
class _CorePainter extends CustomPainter {
  final double t; // eased 0..1 breath phase
  _CorePainter(this.t);

  @override
  void paint(Canvas canvas, Size size) {
    final center = Offset(size.width * 0.40, size.height * 0.44);
    final base = math.min(size.width, size.height);
    final radius = base * (0.40 + 0.06 * t);
    final glow = 0.10 + 0.05 * t;
    final paint = Paint()
      ..shader = RadialGradient(
        colors: [
          const Color(0xFF7E97D8).withValues(alpha: glow),
          const Color(0xFF4E5F8C).withValues(alpha: glow * 0.45),
          const Color(0x00000000),
        ],
        stops: const [0.0, 0.45, 1.0],
      ).createShader(Rect.fromCircle(center: center, radius: radius));
    canvas.drawCircle(center, radius, paint);
  }

  @override
  bool shouldRepaint(_CorePainter old) => old.t != t;
}
