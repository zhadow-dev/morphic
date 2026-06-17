import 'dart:io';
import 'dart:isolate';

import 'package:path/path.dart' as p;

/// Locates the `runtime_assets/` directory shipped with this package.
///
/// Resolution order:
///  1. `Isolate.resolvePackageUri` — canonical; works under `dart run` both from
///     a source checkout and a resolved pub cache (the `dart run morphic:init`
///     path). NOT supported in every runtime (it throws under `flutter test`),
///     so it is guarded and falls through.
///  2. A bounded upward walk from `Platform.script` — AOT snapshots / the bin
///     in the pub cache (runtime_assets/ is a sibling of bin/).
///  3. A bounded upward walk from the current directory — the package-root cwd
///     (dev, `flutter test`, `dart run tool/gen_manifest.dart`).
String? _walkUpForAssets(Directory start) {
  var dir = start;
  for (var i = 0; i < 8; i++) {
    final candidate = p.join(dir.path, 'runtime_assets');
    if (Directory(candidate).existsSync()) return candidate;
    final parent = dir.parent;
    if (parent.path == dir.path) break;
    dir = parent;
  }
  return null;
}

Future<String> findRuntimeAssetsRoot() async {
  try {
    final libUri = await Isolate.resolvePackageUri(
      Uri.parse('package:morphic/morphic.dart'),
    );
    if (libUri != null && libUri.scheme == 'file') {
      final packageRoot = p.dirname(p.dirname(libUri.toFilePath()));
      final candidate = p.join(packageRoot, 'runtime_assets');
      if (Directory(candidate).existsSync()) return candidate;
    }
  } catch (_) {
    // resolvePackageUri is unsupported in this runtime (e.g. flutter test) —
    // fall through to the path-based fallbacks.
  }

  final scriptPath = Platform.script.toFilePath();
  if (scriptPath.isNotEmpty) {
    final found = _walkUpForAssets(Directory(p.dirname(scriptPath)));
    if (found != null) return found;
  }

  final fromCwd = _walkUpForAssets(Directory.current);
  if (fromCwd != null) return fromCwd;

  throw StateError(
    'Could not locate the morphic runtime_assets/ directory. '
    'Reinstall morphic, or run `dart run morphic:init` from the package root.',
  );
}
