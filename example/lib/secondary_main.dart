import 'dart:math';
import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/services.dart';

import 'editor_pane.dart';

/// Secondary Flutter entrypoint for Morphic renderer surfaces.
/// This runs in a SEPARATE Flutter engine inside a Morphic surface HWND.
///
/// Phase 2A.5: Includes frame cadence telemetry and animation suppression.
///
/// Must be decorated with @pragma('vm:entry-point') to survive tree-shaking.
@pragma('vm:entry-point')
void secondaryMain() {
  runApp(const MorphicSurfaceApp());
}

class MorphicSurfaceApp extends StatelessWidget {
  const MorphicSurfaceApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true),
      home: const MorphicSurfaceContent(),
    );
  }
}

class MorphicSurfaceContent extends StatefulWidget {
  const MorphicSurfaceContent({super.key});

  @override
  State<MorphicSurfaceContent> createState() => _MorphicSurfaceContentState();
}

class _MorphicSurfaceContentState extends State<MorphicSurfaceContent>
    with TickerProviderStateMixin, WidgetsBindingObserver {
  late AnimationController _pulseAnim;
  late AnimationController _scrollAnim;
  late AnimationController _rotateAnim;
  bool _stressMode = false;
  bool _animationsPaused = false;
  bool _isEditorMode = true; // Phase 10: Default to Editor Mode

  // Frame cadence telemetry
  static const _channel = MethodChannel('morphic');
  int _frameCount = 0;
  int _engineId = -1;

  // Phase 2B: Lifecycle state tracking

  // Phase 2B.1: Recovery instrumentation
  int? _resumeTimestampUs;      // microsecond timestamp of last resume
  int? _firstFrameAfterResumeUs; // first frame timestamp after resume
  double? _lastRecoveryMs;       // computed recovery latency
  int _frameCountAtPark = 0;     // frame count when parked
  int _resumeCycleCount = 0;     // how many park/resume cycles

  @override
  void initState() {
    super.initState();
    _pulseAnim = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 3),
    )..repeat(reverse: true);

    _scrollAnim = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 8),
    )..repeat();

    _rotateAnim = AnimationController(
      vsync: this,
      duration: const Duration(seconds: 12),
    )..repeat();

    // Register persistent frame callback for cadence tracking
    SchedulerBinding.instance.addPersistentFrameCallback(_onFrame);

    // Listen for commands from native (pause/resume/query)
    _channel.setMethodCallHandler(_handleNativeCommand);

    // Assign engine ID from hashCode (unique per isolate)
    _engineId = identityHashCode(this) % 10000;

    // Phase 2B: Register lifecycle observer
    WidgetsBinding.instance.addObserver(this);
  }

  // Phase 2B: Track lifecycle state changes from native orchestration
  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    final msg = 'Lifecycle: ${state.name}';
    debugPrint('[ENGINE $_engineId] $msg');

    // Recovery instrumentation: track resume timestamp
    if (state == AppLifecycleState.resumed) {
      _resumeTimestampUs = DateTime.now().microsecondsSinceEpoch;
      _firstFrameAfterResumeUs = null; // reset — waiting for first frame
      _resumeCycleCount++;
      debugPrint('[ENGINE $_engineId] RECOVERY: resume received at $_resumeTimestampUs (cycle $_resumeCycleCount)');
    }

    // Track frame count at park for delta calculation
    if (state == AppLifecycleState.paused ||
        state == AppLifecycleState.hidden) {
      _frameCountAtPark = _frameCount;
    }

    if (mounted) {
      setState(() {
      });
    }
    // Report lifecycle change to native for telemetry
    try {
      _channel.invokeMethod('frameProduced', {
        'engineId': _engineId,
        'frameCount': _frameCount,
        'timestampMs': 0.0,
        'lifecycleState': state.name,
      });
    } catch (_) {}
  }

  void _onFrame(Duration timestamp) {
    _frameCount++;

    // Recovery instrumentation: measure first frame after resume
    if (_resumeTimestampUs != null && _firstFrameAfterResumeUs == null) {
      _firstFrameAfterResumeUs = DateTime.now().microsecondsSinceEpoch;
      _lastRecoveryMs = (_firstFrameAfterResumeUs! - _resumeTimestampUs!) / 1000.0;
      debugPrint('[ENGINE $_engineId] RECOVERY: first frame after ${_lastRecoveryMs!.toStringAsFixed(1)}ms (cycle $_resumeCycleCount)');

      // Report recovery event immediately (don't wait for throttle)
      try {
        _channel.invokeMethod('frameProduced', {
          'engineId': _engineId,
          'frameCount': _frameCount,
          'timestampMs': timestamp.inMicroseconds / 1000.0,
          'recoveryMs': _lastRecoveryMs,
          'resumeCycle': _resumeCycleCount,
          'framesWhileParked': _frameCount - _frameCountAtPark,
        });
      } catch (_) {}
    }

    // Report to native only every ~60 frames (~1Hz at 60fps)
    if (_frameCount % 60 != 0) return;
    try {
      _channel.invokeMethod('frameProduced', {
        'engineId': _engineId,
        'frameCount': _frameCount,
        'timestampMs': timestamp.inMicroseconds / 1000.0,
      });
    } catch (_) {
      // Ignore if native handler not yet registered
    }
  }

  Future<dynamic> _handleNativeCommand(MethodCall call) async {
    switch (call.method) {
      case 'pauseAnimations':
        _pauseAnimations();
        return {'success': true, 'engineId': _engineId};
      case 'resumeAnimations':
        _resumeAnimations();
        return {'success': true, 'engineId': _engineId};
      case 'queryLocalCadence':
        return {
          'engineId': _engineId,
          'frameCount': _frameCount,
          'animationsPaused': _animationsPaused,
          'stressMode': _stressMode,
          'lastRecoveryMs': _lastRecoveryMs,
          'resumeCycleCount': _resumeCycleCount,
          'frameCountAtPark': _frameCountAtPark,
        };
      default:
        return null;
    }
  }

  void _pauseAnimations() {
    if (_animationsPaused) return;
    _animationsPaused = true;
    _pulseAnim.stop();
    _scrollAnim.stop();
    _rotateAnim.stop();
    if (mounted) setState(() {});
  }

  void _resumeAnimations() {
    if (!_animationsPaused) return;
    _animationsPaused = false;
    _pulseAnim.repeat(reverse: true);
    _scrollAnim.repeat();
    _rotateAnim.repeat();
    if (mounted) setState(() {});
  }

  @override
  void dispose() {
    _pulseAnim.dispose();
    _scrollAnim.dispose();
    _rotateAnim.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (_isEditorMode) {
      return Stack(
        children: [
          EditorPane(engineId: _engineId),
          Positioned(
            bottom: 8,
            right: 8,
            child: GestureDetector(
              onTap: () => setState(() => _isEditorMode = false),
              child: Container(
                padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                decoration: BoxDecoration(
                  color: const Color(0xFF1f6feb),
                  borderRadius: BorderRadius.circular(6),
                ),
                child: const Text('SWITCH TO STRESS UI', style: TextStyle(fontSize: 9, color: Colors.white)),
              ),
            ),
          ),
        ],
      );
    }

    return Scaffold(
      backgroundColor: const Color(0xFF0a0e1a),
      body: Stack(
        children: [
          // Layer 0: Animated gradient background
          _buildAnimatedBackground(),

          // Layer 1: Main content — scrolling list with animated items
          _buildAnimatedScrollList(),

          // Layer 2: Floating overlays with opacity + transforms
          _buildFloatingOverlays(),

          // Layer 3: Header with status
          _buildStressHeader(),

          // Layer 4: Toggle buttons
          Positioned(
            bottom: 8,
            right: 8,
            child: Row(
              children: [
                GestureDetector(
                  onTap: () => setState(() => _isEditorMode = true),
                  child: Container(
                    margin: const EdgeInsets.only(right: 8),
                    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                    decoration: BoxDecoration(
                      color: const Color(0xFF3fb950),
                      borderRadius: BorderRadius.circular(6),
                    ),
                    child: const Text('SWITCH TO EDITOR', style: TextStyle(fontSize: 9, color: Colors.white)),
                  ),
                ),
                GestureDetector(
                  onTap: () => setState(() => _stressMode = !_stressMode),
                  child: Container(
                    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 5),
                    decoration: BoxDecoration(
                      color: _stressMode
                          ? const Color(0xFFE94560)
                          : const Color(0xFF1A1A2E),
                      borderRadius: BorderRadius.circular(6),
                      border: Border.all(color: Colors.white24),
                    ),
                    child: Text(
                      _stressMode ? 'STRESS ON' : 'STRESS OFF',
                      style: const TextStyle(
                        fontSize: 9,
                        fontWeight: FontWeight.w700,
                        color: Colors.white70,
                        letterSpacing: 1,
                      ),
                    ),
                  ),
                ),
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildAnimatedBackground() {
    return AnimatedBuilder(
      animation: _pulseAnim,
      builder: (context, _) {
        final v = _animationsPaused ? 0.5 : _pulseAnim.value;
        return Container(
          decoration: BoxDecoration(
            gradient: LinearGradient(
              begin: Alignment(-1.0 + v * 0.5, -1.0 + v * 0.3),
              end: Alignment(1.0 - v * 0.3, 1.0 - v * 0.5),
              colors: [
                Color.lerp(const Color(0xFF0a0e1a), const Color(0xFF16213E), v)!,
                Color.lerp(const Color(0xFF16213E), const Color(0xFF0F3460), v * 0.7)!,
                Color.lerp(const Color(0xFF0F3460), const Color(0xFF533483), v * 0.5)!,
              ],
            ),
          ),
        );
      },
    );
  }

  Widget _buildAnimatedScrollList() {
    return AnimatedBuilder(
      animation: Listenable.merge([_scrollAnim, _pulseAnim]),
      builder: (context, _) {
        return ListView.builder(
          padding: const EdgeInsets.only(top: 48, bottom: 40, left: 8, right: 8),
          itemCount: _stressMode ? 60 : 20,
          itemBuilder: (context, index) => _buildAnimatedListItem(index),
        );
      },
    );
  }

  Widget _buildAnimatedListItem(int index) {
    final sv = _animationsPaused ? 0.0 : _scrollAnim.value;
    final phase = (sv * 2 * pi) + (index * 0.3);
    final opacity = 0.5 + 0.5 * sin(phase);
    final translateX = sin(phase * 0.7) * (_stressMode ? 12.0 : 4.0);
    final scale = 0.95 + 0.05 * sin(phase * 1.3);

    final hue = ((index * 37 + 120) % 360).toDouble();
    final itemColor = HSLColor.fromAHSL(1.0, hue, 0.6, 0.4).toColor();

    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 3),
      child: Transform.translate(
        offset: Offset(translateX, 0),
        child: Transform.scale(
          scale: scale,
          child: Opacity(
            opacity: opacity.clamp(0.3, 1.0),
            child: Container(
              height: _stressMode ? 56 : 44,
              decoration: BoxDecoration(
                borderRadius: BorderRadius.circular(8),
                gradient: LinearGradient(
                  colors: [
                    itemColor.withValues(alpha: 0.7),
                    itemColor.withValues(alpha: 0.3),
                  ],
                ),
                boxShadow: _stressMode
                    ? [
                        BoxShadow(
                          color: itemColor.withValues(alpha: 0.3 * opacity),
                          blurRadius: 8 + 4 * sin(phase),
                          offset: Offset(0, 2 + sin(phase) * 2),
                        ),
                      ]
                    : null,
                border: Border.all(
                  color: Colors.white.withValues(alpha: 0.1 + 0.1 * sin(phase)),
                ),
              ),
              child: Row(
                children: [
                  const SizedBox(width: 12),
                  Transform.rotate(
                    angle: _stressMode ? phase * 0.5 : 0,
                    child: Icon(
                      _getIcon(index),
                      size: 18,
                      color: Colors.white.withValues(alpha: 0.7 + 0.3 * sin(phase)),
                    ),
                  ),
                  const SizedBox(width: 10),
                  Expanded(
                    child: Column(
                      mainAxisAlignment: MainAxisAlignment.center,
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          'Surface Item ${index + 1}',
                          style: TextStyle(
                            fontSize: 11,
                            fontWeight: FontWeight.w600,
                            color: Colors.white.withValues(alpha: 0.9),
                          ),
                        ),
                        if (_stressMode)
                          Text(
                            _animationsPaused
                                ? 'ANIMATIONS PAUSED — measuring idle cadence'
                                : 'Animated stress content',
                            style: TextStyle(
                              fontSize: 8,
                              color: _animationsPaused
                                  ? const Color(0xFFE94560).withValues(alpha: 0.7)
                                  : Colors.white.withValues(alpha: 0.4),
                            ),
                          ),
                      ],
                    ),
                  ),
                  if (_stressMode && !_animationsPaused)
                    SizedBox(
                      width: 20,
                      height: 20,
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        value: (sin(phase * 2) * 0.5 + 0.5),
                        color: Colors.white38,
                      ),
                    ),
                  const SizedBox(width: 12),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildFloatingOverlays() {
    if (!_stressMode || _animationsPaused) return const SizedBox.shrink();

    return AnimatedBuilder(
      animation: _rotateAnim,
      builder: (context, _) {
        return Stack(
          children: List.generate(5, (i) {
            final phase = _rotateAnim.value * 2 * pi + (i * 1.2);
            final x = 50.0 + sin(phase) * 60;
            final y = 100.0 + cos(phase * 0.7) * 80 + i * 50;
            final opacity = 0.15 + 0.1 * sin(phase * 2);

            return Positioned(
              left: x,
              top: y,
              child: Transform.rotate(
                angle: phase * 0.3,
                child: Container(
                  width: 50 + i * 8.0,
                  height: 50 + i * 8.0,
                  decoration: BoxDecoration(
                    borderRadius: BorderRadius.circular(12),
                    gradient: RadialGradient(
                      colors: [
                        const Color(0xFFE94560).withValues(alpha: opacity),
                        const Color(0xFF533483).withValues(alpha: opacity * 0.3),
                      ],
                    ),
                    boxShadow: [
                      BoxShadow(
                        color: const Color(0xFFE94560).withValues(alpha: opacity * 0.5),
                        blurRadius: 16,
                        spreadRadius: 2,
                      ),
                    ],
                  ),
                ),
              ),
            );
          }),
        );
      },
    );
  }

  Widget _buildStressHeader() {
    return AnimatedBuilder(
      animation: _pulseAnim,
      builder: (context, _) {
        final v = _animationsPaused ? 0.5 : _pulseAnim.value;
        return Container(
          height: 44,
          decoration: BoxDecoration(
            gradient: LinearGradient(
              colors: [
                const Color(0xFF0a0e1a).withValues(alpha: 0.95),
                const Color(0xFF0a0e1a).withValues(alpha: 0.7),
              ],
              begin: Alignment.topCenter,
              end: Alignment.bottomCenter,
            ),
            boxShadow: [
              BoxShadow(
                color: const Color(0xFFE94560).withValues(alpha: 0.1 * v),
                blurRadius: 12,
                offset: const Offset(0, 2),
              ),
            ],
          ),
          child: Row(
            children: [
              const SizedBox(width: 12),
              Icon(Icons.layers, size: 16,
                color: Color.lerp(const Color(0xFFE94560), const Color(0xFF0F3460), v)),
              const SizedBox(width: 8),
              Text('MORPHIC',
                style: TextStyle(
                  fontSize: 12, fontWeight: FontWeight.w800, letterSpacing: 4,
                  color: Colors.white.withValues(alpha: 0.8 + 0.2 * v),
                )),
              const Spacer(),
              Text(
                _animationsPaused
                    ? '⏸ PAUSED'
                    : _stressMode ? '▲ STRESS' : '● IDLE',
                style: TextStyle(
                  fontSize: 9, fontWeight: FontWeight.w600, letterSpacing: 1.5,
                  color: _animationsPaused
                      ? Colors.amber
                      : _stressMode ? const Color(0xFFE94560) : Colors.white38,
                ),
              ),
              const SizedBox(width: 8),
              Text(
                'F:$_frameCount',
                style: const TextStyle(fontSize: 8, color: Colors.white30),
              ),
              const SizedBox(width: 12),
            ],
          ),
        );
      },
    );
  }

  IconData _getIcon(int index) {
    const icons = [
      Icons.dashboard, Icons.folder_open, Icons.settings, Icons.analytics,
      Icons.notifications, Icons.code, Icons.terminal, Icons.storage,
      Icons.memory, Icons.speed, Icons.sync, Icons.cloud,
    ];
    return icons[index % icons.length];
  }
}
