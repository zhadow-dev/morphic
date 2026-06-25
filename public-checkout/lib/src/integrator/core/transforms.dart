import 'dart:io';

import 'manifest.dart';

/// Marker fences. Everything the transformer adds to a developer-owned file is
/// wrapped in these so it is (a) idempotent — re-running detects the fence and
/// skips — and (b) cleanly removable — strip between the fences.
const String kFenceBegin =
    '# >>> MORPHIC RUNTIME (managed by morphic) — do not edit inside <<<';
const String kFenceEnd = '# <<< MORPHIC RUNTIME (managed by morphic) >>>';

enum TransformStatus {
  /// The file was (or, on dry-run, would be) changed.
  applied,

  /// Already in the desired state; nothing to do.
  skipped,

  /// The transform could not be performed (missing file, no anchor, …).
  failed,
}

class TransformResult {
  TransformResult(this.status, this.note);

  TransformResult.applied(this.note) : status = TransformStatus.applied;
  TransformResult.skipped(this.note) : status = TransformStatus.skipped;
  TransformResult.failed(this.note) : status = TransformStatus.failed;

  final TransformStatus status;
  final String note;

  bool get changed => status == TransformStatus.applied;
  bool get failed => status == TransformStatus.failed;
}

/// The newline sequence dominant in [text], so patched files keep their
/// existing line-ending style.
String detectEol(String text) => text.contains('\r\n') ? '\r\n' : '\n';

/// Patches a project's `windows/runner/CMakeLists.txt` to build the Morphic
/// runtime sources: source list, C++20, dwmapi/winmm, include dirs.
///
/// Conservative and version-tolerant by design: the vanilla
/// `add_executable(...)` block is never rewritten. A fenced block is appended
/// after it using `target_sources` / `set_target_properties` /
/// `target_link_libraries` / `target_include_directories`, which compose
/// additively with whatever the present Flutter version generated.
class CMakePatcher {
  CMakePatcher(this.runnerCmakePath, this.manifest);

  final String runnerCmakePath;
  final RuntimeManifest manifest;

  bool get alreadyPatched {
    final file = File(runnerCmakePath);
    return file.existsSync() && file.readAsStringSync().contains(kFenceBegin);
  }

  TransformResult apply({bool dryRun = false}) {
    final file = File(runnerCmakePath);
    if (!file.existsSync()) {
      return TransformResult.failed(
        'runner CMakeLists.txt not found at $runnerCmakePath',
      );
    }
    final original = file.readAsStringSync();
    if (original.contains(kFenceBegin)) {
      return TransformResult.skipped('CMake already patched (fence present).');
    }
    final eol = detectEol(original);
    final sources = _cppSources();
    final block = _buildBlock(sources).replaceAll('\n', eol);
    if (!dryRun) file.writeAsStringSync('$original$eol$block$eol');
    return TransformResult.applied(
      'appended fenced runtime block (${sources.length} .cpp sources).',
    );
  }

  /// Runtime .cpp sources, relative to windows/runner/ where they are
  /// materialized. Headers need no listing; they resolve via include dirs.
  List<String> _cppSources() =>
      manifest.files
          .where((e) => e.target.endsWith('.cpp'))
          .map((e) => e.target.replaceFirst('windows/runner/', ''))
          .toList()
        ..sort();

  String _buildBlock(List<String> cppTargets) {
    final srcLines = cppTargets.map((s) => '  "$s"').join('\n');
    // PREMIUM ASSET GATE — the spatial compositor's GPU link stack and the
    // MORPHIC_SPATIAL define are emitted ONLY for the spatial tier. The native
    // tier has no compositor_ng/ sources, so it must not link the GPU libs nor
    // define MORPHIC_SPATIAL (which gates the compositor wiring in the runtime).
    final spatialBlock =
        manifest.spatial
            ? '''
# Morphic NG spatial compositor — premium tier. GPU stack + the wiring define.
target_compile_definitions(\${BINARY_NAME} PRIVATE "MORPHIC_SPATIAL")
target_link_libraries(\${BINARY_NAME} PRIVATE "d3d11.lib")
target_link_libraries(\${BINARY_NAME} PRIVATE "d2d1.lib")
target_link_libraries(\${BINARY_NAME} PRIVATE "dcomp.lib")
target_link_libraries(\${BINARY_NAME} PRIVATE "windowsapp.lib")
'''
            : '';
    return '''
$kFenceBegin
# Morphic runtime sources, materialized by `morphic init`. They build into the
# runner target alongside Flutter's generated registrant.
target_sources(\${BINARY_NAME} PRIVATE
$srcLines
)
set_target_properties(\${BINARY_NAME} PROPERTIES CXX_STANDARD 20)
set_target_properties(\${BINARY_NAME} PROPERTIES CXX_STANDARD_REQUIRED ON)
target_compile_definitions(\${BINARY_NAME} PRIVATE "NOMINMAX")
target_link_libraries(\${BINARY_NAME} PRIVATE "dwmapi.lib")
target_link_libraries(\${BINARY_NAME} PRIVATE "winmm.lib")
target_link_libraries(\${BINARY_NAME} PRIVATE "dbghelp.lib")
# psapi: the resource sampler (resource_counters) used by the retention pool +
# churn harness — a NATIVE-tier dependency (GetProcessMemoryInfo/GetGuiResources).
target_link_libraries(\${BINARY_NAME} PRIVATE "psapi.lib")
$spatialBlock# Let `#include "validation/foo.h"` resolve from the runner root.
target_include_directories(\${BINARY_NAME} PRIVATE "\${CMAKE_CURRENT_SOURCE_DIR}")
# The morphic plugin's source root (Flutter auto-creates this symlink from the
# pubspec dependency) — needed for the runtime's extension-seam include.
target_include_directories(\${BINARY_NAME} PRIVATE
  "\${CMAKE_SOURCE_DIR}/flutter/ephemeral/.plugin_symlinks/morphic/windows")
$kFenceEnd''';
  }
}

/// Removes the fenced Morphic block from a CMakeLists.txt. Needs no manifest,
/// so `remove` works even when the bundled assets are unavailable.
TransformResult stripCmakeFence(String runnerCmakePath, {bool dryRun = false}) {
  final file = File(runnerCmakePath);
  if (!file.existsSync()) {
    return TransformResult.failed('runner CMakeLists.txt not found.');
  }
  final text = file.readAsStringSync();
  final begin = text.indexOf(kFenceBegin);
  final end = text.indexOf(kFenceEnd);
  if (begin == -1 || end == -1 || end < begin) {
    return TransformResult.skipped('no Morphic fence to strip.');
  }
  final eol = detectEol(text);
  final before = text.substring(0, begin).replaceAll(RegExp(r'(\r?\n)+$'), eol);
  final after = text
      .substring(end + kFenceEnd.length)
      .replaceAll(RegExp(r'^(\r?\n)+'), '');
  if (!dryRun) file.writeAsStringSync('$before$after');
  return TransformResult.applied('stripped fenced runtime block.');
}

/// Ensures `morphic` is a dependency in the project's pubspec.yaml.
///
/// Line-based and idempotent. A full YAML rewrite is intentionally avoided:
/// pubspecs are developer-owned files with comments and formatting that a
/// round-trip through a YAML emitter would destroy.
class PubspecPatcher {
  PubspecPatcher(this.pubspecPath, {required this.morphicDepLines});

  final String pubspecPath;

  /// The dependency entry to insert under `dependencies:`, one line per
  /// element, no trailing newline. E.g. `['  morphic: ^0.1.0']` or
  /// `['  morphic:', '    path: ../morphic']`.
  final List<String> morphicDepLines;

  TransformResult apply({bool dryRun = false}) {
    final file = File(pubspecPath);
    if (!file.existsSync()) {
      return TransformResult.failed('pubspec.yaml not found.');
    }
    final text = file.readAsStringSync();
    if (_morphicDepRegExp.hasMatch(text)) {
      return TransformResult.skipped('morphic already a dependency.');
    }
    final match = RegExp(
      r'^dependencies:[ \t]*\r?$',
      multiLine: true,
    ).firstMatch(text);
    if (match == null) {
      return TransformResult.failed(
        'no top-level `dependencies:` block found in pubspec.',
      );
    }
    final eol = detectEol(text);
    // Insert the dep entry as whole lines directly below `dependencies:`.
    final lineBreak = text.indexOf('\n', match.start);
    final String patched;
    if (lineBreak == -1) {
      patched = text + eol + morphicDepLines.join(eol);
    } else {
      final afterLine = lineBreak + 1;
      final insertion = morphicDepLines.map((l) => '$l$eol').join();
      patched =
          text.substring(0, afterLine) + insertion + text.substring(afterLine);
    }
    if (!dryRun) file.writeAsStringSync(patched);
    return TransformResult.applied('added morphic dependency to pubspec.');
  }
}

final RegExp _morphicDepRegExp = RegExp(r'^\s+morphic\s*:', multiLine: true);

/// Removes the `morphic` dependency entry (including an indented `path:` /
/// `git:` continuation block) from a pubspec.yaml.
TransformResult stripMorphicDependency(
  String pubspecPath, {
  bool dryRun = false,
}) {
  final file = File(pubspecPath);
  if (!file.existsSync()) {
    return TransformResult.failed('pubspec.yaml not found.');
  }
  final text = file.readAsStringSync();
  if (!_morphicDepRegExp.hasMatch(text)) {
    return TransformResult.skipped('morphic is not a dependency.');
  }
  final eol = detectEol(text);
  final lines = text.split(eol);
  final out = <String>[];
  var skipping = false;
  for (final line in lines) {
    if (RegExp(r'^\s+morphic\s*:').hasMatch(line)) {
      skipping = true;
      continue;
    }
    if (skipping) {
      // Continuation lines of the dep entry are indented deeper (path:/git:
      // blocks). Anything else — including blank lines — ends the entry.
      if (RegExp(r'^\s{4,}\S').hasMatch(line)) continue;
      skipping = false;
    }
    out.add(line);
  }
  if (!dryRun) file.writeAsStringSync(out.join(eol));
  return TransformResult.applied('removed morphic dependency from pubspec.');
}
