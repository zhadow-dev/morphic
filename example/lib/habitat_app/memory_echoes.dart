import 'dart:math' as math;

import 'package:flutter/material.dart';

import '../morphic_app/atmosphere_profile.dart';

/// MORPHIC SHOWCASE — The Habitat · MEMORY ECHOES (orbital companion).
///
/// Faint fragments of prior context, drifting at low vitality — NOT a list, NOT a feed, NOT history.
/// Peripheral, half-remembered, glanceable. Each echo breathes its own slow opacity so the surface
/// feels alive and porous rather than static. It demands nothing; it is just nearby cognition.
@pragma('vm:entry-point')
void memoryEchoesMain() => runApp(const _EchoesApp());

class _EchoesApp extends StatelessWidget {
  const _EchoesApp();
  @override
  Widget build(BuildContext context) => MaterialApp(
        debugShowCheckedModeBanner: false,
        theme: ThemeData.dark(useMaterial3: true).copyWith(
          scaffoldBackgroundColor: Colors.transparent,
        ),
        home: const MorphicAtmosphere(
            profile: AtmosphereProfile.echo, child: _Echoes()),
      );
}

class _Echoes extends StatefulWidget {
  const _Echoes();
  @override
  State<_Echoes> createState() => _EchoesState();
}

class _EchoesState extends State<_Echoes> with SingleTickerProviderStateMixin {
  late final AnimationController _drift;

  // Half-remembered fragments — deliberately incomplete, evocative, non-actionable.
  static const _fragments = [
    'the message you didn’t send',
    'that idea about light',
    'a name, almost',
    'last tuesday',
    'the unfinished sketch',
    'something about gradients',
  ];

  @override
  void initState() {
    super.initState();
    _drift = AnimationController(vsync: this, duration: const Duration(seconds: 18))
      ..repeat();
  }

  @override
  void dispose() {
    _drift.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.transparent,
      body: Padding(
        padding: const EdgeInsets.fromLTRB(22, 22, 16, 22),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'echoes',
              style: TextStyle(
                color: Color(0xFF49525C),
                fontSize: 10,
                letterSpacing: 2.8,
                fontWeight: FontWeight.w600,
              ),
            ),
            const SizedBox(height: 16),
            Expanded(
              child: RepaintBoundary(
                child: AnimatedBuilder(
                  animation: _drift,
                  builder: (context, _) => Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                    children: List.generate(_fragments.length, (i) {
                      final phase = i * (math.pi * 2 / _fragments.length);
                      final wave =
                          (math.sin(_drift.value * math.pi * 2 + phase) + 1) / 2; // 0..1
                      final base = 0.20 + (i.isEven ? 0.10 : 0.0);
                      final opacity = (base + 0.16 * wave).clamp(0.0, 1.0);
                      return Padding(
                        padding: EdgeInsets.only(left: i.isOdd ? 14.0 : 0.0),
                        child: Text(
                          _fragments[i],
                          style: TextStyle(
                            color: const Color(0xFFC9D4E0).withOpacity(opacity),
                            fontSize: i.isEven ? 14.0 : 12.5,
                            fontWeight: FontWeight.w300,
                            height: 1.3,
                          ),
                        ),
                      );
                    }),
                  ),
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }
}
