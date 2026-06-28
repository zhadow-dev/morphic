// Regenerates runtime_assets/manifest.json from the bundled runtime source
// tree. Run from the package root:  dart run tool/gen_manifest.dart
//
// The manifest is GENERATED, never hand-edited — deriving it from the actual
// asset tree keeps it from drifting from what ships.
import 'dart:convert';
import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:path/path.dart' as p;

const String kRuntimeVersion = '0.2.0';

/// The pub.dev package version, read from the package's own pubspec — the
/// single source of truth. Distinct from [kRuntimeVersion] (the engine ABI).
String _readPackageVersion() {
  final pubspec = File(p.join(Directory.current.path, 'pubspec.yaml'));
  for (final line in pubspec.readAsLinesSync()) {
    final m = RegExp(r'^version:\s*(\S+)').firstMatch(line);
    if (m != null) return m.group(1)!;
  }
  throw StateError('no `version:` in pubspec.yaml — run from package root.');
}

void main() {
  final assetsRoot = p.join(Directory.current.path, 'runtime_assets');
  final runnerSrc = p.join(assetsRoot, 'windows', 'runner_morphic');
  final packageVersion = _readPackageVersion();

  if (!Directory(runnerSrc).existsSync()) {
    stderr.writeln(
      'runtime_assets/windows/runner_morphic not found — run from package root.',
    );
    exit(1);
  }

  final files = <ManifestEntry>[];
  for (final f
      in Directory(runnerSrc).listSync(recursive: true).whereType<File>()) {
    final rel = p.relative(f.path, from: runnerSrc).replaceAll('\\', '/');
    // main.cpp and CMakeLists.txt are NEVER plain runtime sources: the
    // entrypoint ships as the separate `runner_main_morphic.cpp` template
    // (mainReplacement) and the project's own CMakeLists is patched in place by
    // CMakePatcher. Skip any stray copies so they can't pollute the manifest.
    if (rel == 'main.cpp' || rel == 'CMakeLists.txt') continue;
    files.add(
      ManifestEntry(
        source: 'windows/runner_morphic/$rel',
        target: 'windows/runner/$rel',
        sha256: hashFile(f),
        kind: ManifestEntry.kindRuntimeSource,
      ),
    );
  }
  files.sort((a, b) => a.target.compareTo(b.target));

  // The main.cpp template REPLACES the project's runner/main.cpp; the original
  // is backed up by init.
  final mainTemplate = File(
    p.join(assetsRoot, 'windows', 'runner_main_morphic.cpp'),
  );
  final mainReplacement = ManifestEntry(
    source: 'windows/runner_main_morphic.cpp',
    target: 'windows/runner/main.cpp',
    sha256: hashFile(mainTemplate),
    kind: ManifestEntry.kindMainReplacement,
  );

  // PREMIUM ASSET GATE (Phase 1) — tier the manifest. The native (free) tier
  // excludes the spatial compositor (`compositor_ng/`); the spatial (premium)
  // tier is the full set. Two separate manifest files so the premium tier can
  // later be delivered independently (downloaded only after license validation).
  bool isSpatialOnly(ManifestEntry e) =>
      e.target.contains('/compositor_ng/');

  final nativeFiles = files.where((e) => !isSpatialOnly(e)).toList();

  void write({required bool spatial, required List<ManifestEntry> tierFiles}) {
    final manifest = RuntimeManifest(
      runtimeVersion: kRuntimeVersion,
      packageVersion: packageVersion,
      spatial: spatial,
      mainReplacement: mainReplacement,
      files: tierFiles,
    );
    final json = <String, Object>{
      ...manifest.toJson(),
      'generated': DateTime.now().toUtc().toIso8601String(),
    };
    final out = File(
      p.join(assetsRoot, RuntimeManifest.fileNameFor(spatial: spatial)),
    );
    out.writeAsStringSync(
      '${const JsonEncoder.withIndent('  ').convert(json)}\n',
    );
    stdout.writeln(
      'Wrote ${out.path}: ${tierFiles.length} runtime sources + 1 main '
      'replacement (${spatial ? 'SPATIAL' : 'native'} tier, '
      'package $packageVersion, engine $kRuntimeVersion).',
    );
  }

  write(spatial: false, tierFiles: nativeFiles);
  write(spatial: true, tierFiles: files);
}
