import 'dart:convert';
import 'dart:io';

import 'package:args/command_runner.dart';

import '../core/install_engine.dart';
import '../core/manifest.dart';
import '../core/project.dart';
import '../core/transforms.dart';
import 'cli_environment.dart';

/// `morphic init` — transform the current Flutter project's Windows runner
/// into a Morphic-hosted runtime.
///
/// Model: runtime sources ship as package assets and are materialized locally
/// into the project tree — the app owns its runtime instance (like Expo
/// prebuild / FlutterFire / Tauri init). Every mutation goes through the
/// [InstallEngine] so `morphic remove` can reverse it exactly.
///
/// Safety: dry-run by default; `--apply` mutates. Idempotent (install record
/// and fences short-circuit). Reversible (backups + manifest). Non-Windows
/// platforms and `lib/` are never touched.
class InitCommand extends Command<int> {
  InitCommand(this.env) {
    argParser
      ..addFlag(
        'apply',
        negatable: false,
        help: 'Perform the transform. Without this, prints a dry-run plan.',
      )
      ..addFlag(
        'force',
        negatable: false,
        help:
            'Re-install: reverses an existing install first, then '
            'installs fresh.',
      )
      ..addFlag(
        'json',
        negatable: false,
        help: 'Emit the plan/result as JSON.',
      )
      ..addFlag(
        'spatial',
        negatable: false,
        help:
            'Install the SPATIAL (premium) tier — the GPU compositor, materials, '
            'and shaped surfaces. Requires an activated license; without it the '
            'native multi-window runtime is installed.',
      );
  }

  final CliEnvironment env;

  @override
  final String name = 'init';
  @override
  final String description =
      'Transform this Flutter project into a Morphic-hosted runtime (Windows).';

  @override
  Future<int> run() async {
    final apply = argResults!.flag('apply');
    final force = argResults!.flag('force');
    final asJson = argResults!.flag('json');
    final spatial = argResults!.flag('spatial');

    final project = FlutterProject.locate(env.workingDirectory);
    if (project == null) {
      return _fail(
        asJson,
        'No Flutter project found (no pubspec.yaml in cwd or any parent).',
      );
    }
    if (!project.isFlutterApp) {
      return _fail(
        asJson,
        '${project.root} is not a Flutter app (no flutter sdk dependency).',
      );
    }
    if (!project.hasRunner) {
      return _fail(
        asJson,
        'No windows/runner found. Run: flutter create --platforms=windows .',
      );
    }

    final assetsRoot = await env.resolveAssetsRoot();
    final RuntimeManifest manifest;
    try {
      manifest = RuntimeManifest.loadBundled(assetsRoot, spatial: spatial);
    } on StateError catch (e) {
      // The spatial tier is a premium add-on that may not be present (it is
      // delivered only after license activation). Fail with a clear message
      // rather than a stack trace.
      return _fail(asJson, e.message);
    }
    final engine = InstallEngine(project.root, assetsRoot: assetsRoot);

    final alreadyInstalled = engine.isInstalled;
    if (alreadyInstalled && !force) {
      // Version-aware idempotency: compare the INSTALLED runtime ABI (from the
      // project's install record) against the PACKAGED runtime ABI (this
      // package's bundled manifest). Equal → nothing to do. Different → tell the
      // user; never silently leave them on an older runtime.
      final rec = engine.readRecord()!;
      final installedAbi = rec.runtimeVersion;
      final packagedAbi = manifest.runtimeVersion;
      final cmp = _compareRuntimeAbi(installedAbi, packagedAbi);
      final upgradeAvailable = cmp < 0;

      if (asJson) {
        env.out.writeln(
          jsonEncode({
            'command': 'init',
            'ok': true,
            'alreadyInstalled': true,
            'installedRuntimeVersion': installedAbi,
            'packagedRuntimeVersion': packagedAbi,
            'upgradeAvailable': upgradeAvailable,
          }),
        );
        return MorphicExit.ok;
      }

      if (cmp == 0) {
        // Case 1 — installed runtime matches the packaged runtime.
        env.out.writeln('Runtime already installed.');
        env.out.writeln();
        env.out.writeln('  ABI $packagedAbi');
        env.out.writeln();
        env.out.writeln('Nothing to do.');
      } else if (cmp < 0) {
        // Case 2 — installed runtime is OLDER than the packaged runtime.
        env.out.writeln('Installed runtime:');
        env.out.writeln();
        env.out.writeln('  ABI $installedAbi');
        env.out.writeln();
        env.out.writeln('Available runtime:');
        env.out.writeln();
        env.out.writeln('  ABI $packagedAbi');
        env.out.writeln();
        env.out.writeln('A runtime upgrade is available.');
        env.out.writeln();
        env.out.writeln('Re-run with:');
        env.out.writeln();
        env.out.writeln('  dart run morphic:init --force');
      } else {
        // Edge — installed runtime is NEWER than the packaged runtime (e.g. the
        // morphic package was downgraded). Still never continue silently.
        env.out.writeln('Installed runtime:');
        env.out.writeln();
        env.out.writeln('  ABI $installedAbi');
        env.out.writeln();
        env.out.writeln('Packaged runtime:');
        env.out.writeln();
        env.out.writeln('  ABI $packagedAbi');
        env.out.writeln();
        env.out.writeln(
          "The installed runtime is newer than this package's runtime.",
        );
        env.out.writeln();
        env.out.writeln('To replace it with the packaged runtime, re-run with:');
        env.out.writeln();
        env.out.writeln('  dart run morphic:init --force');
      }
      return MorphicExit.ok;
    }

    final plan = <String>[
      if (alreadyInstalled)
        'reverse the existing install (restore backups) before re-installing',
      'add morphic dependency to pubspec.yaml',
      'materialize ${manifest.files.length} runtime sources into windows/runner/',
      'back up + replace windows/runner/main.cpp (WinMain → MorphicRuntime)',
      'back up + patch windows/runner/CMakeLists.txt (fenced: sources, C++20, '
          'dwmapi/winmm, includes)',
      'write .morphic/install.json (reversal record)',
      'leave lib/, app pubspec config, and non-Windows platforms untouched',
    ];

    if (!asJson) {
      // In the spatial flow this runs as the second of two jobs (delivery is
      // the first, in bin/init.dart) — label it so the two are unmistakable.
      if (manifest.spatial) {
        env.out.writeln('Step 2 of 2 — Project integration');
      }
      env.out.writeln(
        'morphic init — ${apply ? 'APPLY' : 'DRY RUN'} for ${project.root}',
      );
      env.out.writeln();
      void kv(String k, String v) => env.out.writeln('  ${k.padRight(16)}$v');
      if (manifest.packageVersion != null) {
        kv('Package', manifest.packageVersion!);
      }
      kv('Runtime ABI', '${manifest.runtimeVersion}  (stable across releases)');
      kv('License', manifest.spatial ? 'Spatial (Premium)' : 'Native (Free)');
      // Spell the spatial runtime version out HERE, in the install summary —
      // not only during download — so there is zero doubt which runtime this
      // project now hosts. For the spatial tier the package == the artifact.
      if (manifest.spatial && manifest.packageVersion != null) {
        kv('Spatial runtime', manifest.packageVersion!);
      }
      env.out.writeln();
    }

    if (!apply) {
      if (asJson) {
        env.out.writeln(
          jsonEncode({
            'command': 'init',
            'ok': true,
            'mode': 'dry-run',
            'project': project.root,
            'runtimeVersion': manifest.runtimeVersion,
            if (manifest.packageVersion != null)
              'packageVersion': manifest.packageVersion,
            'spatial': manifest.spatial,
            'plan': plan,
          }),
        );
      } else {
        env.out.writeln('  Project changes (planned)');
        for (final step in plan) {
          env.out.writeln('    • $step');
        }
        env.out.writeln(
          '\nDry run only. Re-run with --apply to perform the transform.',
        );
      }
      return MorphicExit.ok;
    }

    final actions = <String>[];

    // A forced re-install reverses the previous install first, so the fresh
    // install backs up the user's pristine files — not Morphic's own output.
    if (alreadyInstalled) {
      stripCmakeFence(project.runnerCmakePath);
      stripMorphicDependency(project.pubspecPath);
      for (final a in engine.reverse()) {
        actions.add('(reversed previous install) $a');
      }
    }

    try {
      final backups = <BackupEntry>[
        engine.backup('windows/runner/main.cpp'),
        engine.backup('windows/runner/CMakeLists.txt'),
        engine.backup('pubspec.yaml'),
      ];

      // Journal the install record BEFORE mutating: if any later step throws
      // (or the process dies), the record on disk already describes how to
      // reverse what was done so far.
      engine.writeRecord(
        InstallRecord(
          runtimeVersion: manifest.runtimeVersion,
          installedAt: DateTime.now().toUtc().toIso8601String(),
          materialized: [
            for (final entry in manifest.all)
              MaterializedFile(target: entry.target, sha256: entry.sha256),
          ],
          backups: backups,
        ),
      );
      actions.add('wrote .morphic/install.json (reversal journal)');

      for (final entry in manifest.all) {
        engine.materialize(entry);
      }
      actions.add('materialized ${manifest.all.length} files');

      final cmakeResult =
          CMakePatcher(project.runnerCmakePath, manifest).apply();
      if (cmakeResult.failed) {
        throw StateError('CMake patch failed: ${cmakeResult.note}');
      }
      actions.add('cmake: ${cmakeResult.note}');

      final pubspecResult =
          PubspecPatcher(
            project.pubspecPath,
            morphicDepLines: _morphicDepLines(),
          ).apply();
      if (pubspecResult.failed) {
        throw StateError('pubspec patch failed: ${pubspecResult.note}');
      }
      actions.add('pubspec: ${pubspecResult.note}');

      if (asJson) {
        env.out.writeln(
          jsonEncode({
            'command': 'init',
            'ok': true,
            'mode': 'apply',
            'project': project.root,
            'runtimeVersion': manifest.runtimeVersion,
            if (manifest.packageVersion != null)
              'packageVersion': manifest.packageVersion,
            'spatial': manifest.spatial,
            'actions': actions,
          }),
        );
      } else {
        env.out.writeln('  Project changes');
        for (final a in actions) {
          env.out.writeln('    ✓ $a');
        }
        // The explicit answer to "is Spatial actually active in THIS project?"
        if (manifest.spatial && manifest.packageVersion != null) {
          env.out.writeln(
            '\n  ✓ Spatial runtime ${manifest.packageVersion} installed — '
            'active in this project.',
          );
        }
        // Installed-components summary — one unambiguous place to confirm exactly
        // what is now active (package version, the stable runtime ABI, and tier).
        env.out.writeln('\nInstalled components');
        env.out.writeln('  ${'─' * 36}');
        void comp(String k, String v) =>
            env.out.writeln('  ${k.padRight(16)}$v');
        if (manifest.packageVersion != null) {
          comp('Package', manifest.packageVersion!);
        }
        comp('Runtime ABI', '${manifest.runtimeVersion}  (stable)');
        comp('Tier', manifest.spatial ? 'Spatial (premium)' : 'Native (free)');
        env.out.writeln('\nReady to build:');
        env.out.writeln(
          '  point your entry at Morphic:  void main() => runMorphicApp(app: MyApp());',
        );
        env.out.writeln('  flutter pub get');
        env.out.writeln('  flutter run -d windows');
        env.out.writeln('\nUndo anytime:  dart run morphic:remove --apply');
        if (!manifest.spatial) {
          // Natural Spatial discovery — no paywall, no pricing, just a pointer.
          env.out.writeln('\n  Native Mode: enabled.');
          env.out.writeln(
            '\nWant Spatial Mode? Shaped surfaces, materials and workspace '
            'composition (free Developer Preview).',
          );
          env.out.writeln('  Sign in:    dart run morphic:login');
          env.out.writeln(
            '  Learn more: https://www.getmorphic.space/spatial',
          );
        }
      }
      return MorphicExit.ok;
    } catch (e) {
      final rollback = engine.reverse();
      if (asJson) {
        env.out.writeln(
          jsonEncode({
            'command': 'init',
            'ok': false,
            'mode': 'apply',
            'error': '$e',
            'rollback': rollback,
          }),
        );
      } else {
        env.err.writeln('\ninit FAILED: $e');
        env.err.writeln('Rolled back automatically:');
        for (final a in rollback) {
          env.err.writeln('  $a');
        }
        env.err.writeln('Project should be at its pre-init state.');
      }
      return MorphicExit.refused;
    }
  }

  int _fail(bool asJson, String message) {
    if (asJson) {
      env.out.writeln(
        jsonEncode({'command': 'init', 'ok': false, 'error': message}),
      );
    } else {
      env.err.writeln(message);
    }
    return MorphicExit.precondition;
  }

  /// Compares two runtime ABI strings (e.g. "0.1.0" vs "0.2.0"). Returns a
  /// negative number if [a] is older than [b], 0 if equal, positive if newer.
  /// Numeric and dot-segmented; falls back to a lexical compare if any segment
  /// is non-numeric (runtime ABIs are always plain X.Y.Z, so the fallback is
  /// purely defensive).
  static int _compareRuntimeAbi(String a, String b) {
    if (a == b) return 0;
    final pa = a.split('.');
    final pb = b.split('.');
    final n = pa.length > pb.length ? pa.length : pb.length;
    for (var i = 0; i < n; i++) {
      final ia = i < pa.length ? int.tryParse(pa[i]) : 0;
      final ib = i < pb.length ? int.tryParse(pb[i]) : 0;
      if (ia == null || ib == null) return a.compareTo(b);
      if (ia != ib) return ia - ib;
    }
    return 0;
  }

  /// How the generated pubspec references the morphic package. If
  /// `MORPHIC_LOCAL_REPO` points at a morphic checkout (in-repo validation and
  /// local dev), emit a path dependency; otherwise a published-version dep.
  List<String> _morphicDepLines() {
    final local = env.variables['MORPHIC_LOCAL_REPO'];
    if (local != null && Directory(local).existsSync()) {
      final norm = local.replaceAll(r'\', '/');
      return ['  morphic:', '    path: $norm'];
    }
    return ['  morphic: ^0.1.0'];
  }
}
