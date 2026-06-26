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
      final rec = engine.readRecord()!;
      final msg =
          'Morphic already installed (runtime ${rec.runtimeVersion}). '
          'Nothing to do. Use --force to re-install, or `morphic remove` first.';
      if (asJson) {
        env.out.writeln(
          jsonEncode({
            'command': 'init',
            'ok': true,
            'alreadyInstalled': true,
            'installedRuntimeVersion': rec.runtimeVersion,
          }),
        );
      } else {
        env.out.writeln(msg);
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
      env.out.writeln(
        'morphic init — ${apply ? 'APPLY' : 'DRY RUN'} for ${project.root}',
      );
      if (manifest.packageVersion != null) {
        env.out.writeln('  Morphic        ${manifest.packageVersion}  (package)');
      }
      env.out.writeln(
        '  Runtime engine ${manifest.runtimeVersion}  (ABI, stable across releases)',
      );
      env.out.writeln(
        '  Tier           ${manifest.spatial ? 'spatial (premium)' : 'native'}',
      );
      for (final step in plan) {
        env.out.writeln('  • $step');
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
        env.out.writeln(
          'Dry run only. Re-run with --apply to perform the transform.',
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
        for (final a in actions) {
          env.out.writeln('  $a');
        }
        env.out.writeln('\nDone. Next:');
        env.out.writeln(
          '  1. point your app entry at Morphic:  void main() => runMorphicApp(app: MyApp());',
        );
        env.out.writeln('  2. flutter pub get && flutter run -d windows');
        env.out.writeln(
          'Undo anytime:  dart run morphic:remove --apply',
        );
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
