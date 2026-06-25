import 'dart:io';

import 'package:path/path.dart' as p;

/// Read-only view of a Flutter project's shape — enough to decide whether
/// Morphic can integrate and to plan the transform.
///
/// Nothing here mutates a project; mutation lives in [InstallEngine] and the
/// transforms, and is only reached through commands gated on `--apply`.
class FlutterProject {
  FlutterProject(this.root);

  /// The project root (the directory containing pubspec.yaml).
  final String root;

  String get pubspecPath => p.join(root, 'pubspec.yaml');
  String get windowsDir => p.join(root, 'windows');
  String get runnerDir => p.join(windowsDir, 'runner');
  String get runnerCmakePath => p.join(runnerDir, 'CMakeLists.txt');
  String get runnerMainPath => p.join(runnerDir, 'main.cpp');

  bool get hasPubspec => File(pubspecPath).existsSync();
  bool get hasWindows => Directory(windowsDir).existsSync();
  bool get hasRunner => Directory(runnerDir).existsSync();

  /// Walks up from [start] looking for the nearest pubspec.yaml.
  static FlutterProject? locate(String start) {
    var dir = Directory(p.absolute(start));
    while (true) {
      if (File(p.join(dir.path, 'pubspec.yaml')).existsSync()) {
        return FlutterProject(dir.path);
      }
      final parent = dir.parent;
      if (parent.path == dir.path) return null;
      dir = parent;
    }
  }

  /// True if pubspec declares a Flutter SDK dependency. Line-based on purpose:
  /// a full YAML parse would add a dependency for a check this shallow.
  bool get isFlutterApp {
    if (!hasPubspec) return false;
    final text = File(pubspecPath).readAsStringSync();
    return RegExp(r'^\s*flutter:\s*$', multiLine: true).hasMatch(text) &&
        text.contains('sdk: flutter');
  }

  /// True once the runner CMake carries the Morphic fence.
  bool get hasMorphicMarker {
    final file = File(runnerCmakePath);
    if (!file.existsSync()) return false;
    return file.readAsStringSync().contains(kMorphicMarker);
  }

  static const String kMorphicMarker =
      '# >>> MORPHIC RUNTIME (managed by morphic)';
}
