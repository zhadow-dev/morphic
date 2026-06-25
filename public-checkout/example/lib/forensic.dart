import 'dart:io';

/// PHASE 1A forensic instrumentation (Dart side).
///
/// Writes timestamped events to `<exe dir>`\morphic_forensic_dart.log. Each line
/// carries a wall-clock stamp so it can be interleaved with the native trace
/// (morphic_forensic.log) into a single startup timeline. Pure instrumentation.
class Forensic {
  static final Stopwatch _sw = Stopwatch()..start();
  static String? _path;

  static void init() {
    try {
      final dir = File(Platform.resolvedExecutable).parent.path;
      _path = '$dir${Platform.pathSeparator}morphic_forensic_dart.log';
      File(_path!).writeAsStringSync('=== MORPHIC FORENSIC TRACE (dart) ===\n',
          flush: true);
    } catch (_) {
      _path = null;
    }
  }

  /// Resolve the log path WITHOUT truncating. Each Morphic surface runs in its
  /// own engine/isolate, where [init]'s statics are not set — a surface calls
  /// this to append to the shared dart trace instead of silently no-opping.
  static void attach() {
    if (_path != null) return;
    try {
      final dir = File(Platform.resolvedExecutable).parent.path;
      _path = '$dir${Platform.pathSeparator}morphic_forensic_dart.log';
    } catch (_) {
      _path = null;
    }
  }

  static void log(String subsystem, String message) {
    final p = _path;
    if (p == null) return;
    final t = _sw.elapsedMicroseconds / 1e6;
    final wall = DateTime.now();
    final hh = wall.hour.toString().padLeft(2, '0');
    final mm = wall.minute.toString().padLeft(2, '0');
    final ss = wall.second.toString().padLeft(2, '0');
    final ms = wall.millisecond.toString().padLeft(3, '0');
    final line =
        '[+${t.toStringAsFixed(3)}][DART:$subsystem] $message  @wall=$hh:$mm:$ss.$ms\n';
    try {
      File(p).writeAsStringSync(line, mode: FileMode.append, flush: true);
    } catch (_) {}
  }
}
