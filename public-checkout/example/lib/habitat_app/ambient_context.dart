import 'dart:async';

import 'package:flutter/material.dart';

import '../morphic_app/atmosphere_profile.dart';

/// MORPHIC SHOWCASE — The Habitat · AMBIENT CONTEXT (orbital companion).
///
/// The soft "now": a live clock, the daypart, a drifting focus. Genuinely alive — the clock ticks —
/// but undemanding and peripheral. No task, no controls, nothing to complete. It is the environment
/// being quietly aware of itself, the way a room is.
@pragma('vm:entry-point')
void ambientContextMain() => runApp(const _AmbientApp());

class _AmbientApp extends StatelessWidget {
  const _AmbientApp();
  @override
  Widget build(BuildContext context) => MaterialApp(
        debugShowCheckedModeBanner: false,
        theme: ThemeData.dark(useMaterial3: true).copyWith(
          scaffoldBackgroundColor: Colors.transparent,
        ),
        home: const MorphicAtmosphere(
            profile: AtmosphereProfile.ambient, child: _Ambient()),
      );
}

class _Ambient extends StatefulWidget {
  const _Ambient();
  @override
  State<_Ambient> createState() => _AmbientState();
}

class _AmbientState extends State<_Ambient> {
  Timer? _tick;
  DateTime _now = DateTime.now();

  static const _weekdays = ['mon', 'tue', 'wed', 'thu', 'fri', 'sat', 'sun'];

  @override
  void initState() {
    super.initState();
    _tick = Timer.periodic(const Duration(seconds: 1), (_) {
      if (mounted) setState(() => _now = DateTime.now());
    });
  }

  @override
  void dispose() {
    _tick?.cancel();
    super.dispose();
  }

  String get _time {
    final h12 = _now.hour % 12 == 0 ? 12 : _now.hour % 12;
    return '$h12:${_now.minute.toString().padLeft(2, '0')}';
  }

  String get _daypart {
    final h = _now.hour;
    if (h < 5) return 'late night';
    if (h < 12) return 'morning';
    if (h < 17) return 'afternoon';
    if (h < 21) return 'evening';
    return 'night';
  }

  @override
  Widget build(BuildContext context) {
    final wd = _weekdays[_now.weekday - 1];
    return Scaffold(
      backgroundColor: Colors.transparent,
      body: Padding(
        padding: const EdgeInsets.fromLTRB(22, 20, 18, 18),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text(
              _time,
              style: const TextStyle(
                color: Color(0xFFD8E2F0),
                fontSize: 34,
                fontWeight: FontWeight.w200,
                letterSpacing: 0.5,
                height: 1.0,
              ),
            ),
            const SizedBox(height: 5),
            Text(
              '$wd · $_daypart',
              style: const TextStyle(
                color: Color(0xFF6E7A8A),
                fontSize: 12,
                letterSpacing: 1.0,
              ),
            ),
            const Spacer(),
            const Text(
              'focus · drifting',
              style: TextStyle(
                color: Color(0xFFAEB6C0),
                fontSize: 13,
                fontWeight: FontWeight.w300,
              ),
            ),
            const SizedBox(height: 7),
            const Text(
              'quiet · indoors',
              style: TextStyle(color: Color(0xFF4A5468), fontSize: 11, letterSpacing: 0.6),
            ),
          ],
        ),
      ),
    );
  }
}
