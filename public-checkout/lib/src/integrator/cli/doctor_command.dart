import 'dart:convert';
import 'dart:io';

import 'package:args/command_runner.dart';

import '../core/manifest.dart';
import '../core/project.dart';
import 'cli_environment.dart';

/// `morphic doctor` — read-only environment + project diagnostics.
///
/// A check is only reported green when it was actually verified; unknown is
/// reported as unknown, never as a pass.
class DoctorCommand extends Command<int> {
  DoctorCommand(this.env) {
    argParser.addFlag(
      'json',
      negatable: false,
      help: 'Emit results as machine-readable JSON.',
    );
  }

  final CliEnvironment env;

  @override
  final String name = 'doctor';
  @override
  final String description =
      'Diagnose the environment and project for Morphic integration (read-only).';

  @override
  Future<int> run() async {
    final asJson = argResults!.flag('json');
    final checks = <_Check>[];

    checks.add(await _checkCommand('flutter', ['--version'], 'Flutter SDK'));
    checks.add(await _checkCommand('cmake', ['--version'], 'CMake'));
    checks.add(
      Platform.isWindows
          ? _Check('Platform is Windows', true, Platform.operatingSystemVersion)
          : _Check(
            'Platform is Windows',
            false,
            'Morphic is Windows-only today (found ${Platform.operatingSystem}).',
          ),
    );
    checks.add(await _checkBundledAssets());

    final project = FlutterProject.locate(env.workingDirectory);
    if (project == null) {
      checks.add(
        _Check(
          'Flutter project found',
          false,
          'No pubspec.yaml in cwd or any parent.',
        ),
      );
    } else {
      checks.add(_Check('Flutter project found', true, project.root));
      checks.add(
        _Check(
          'Is a Flutter app',
          project.isFlutterApp,
          project.isFlutterApp
              ? null
              : 'pubspec has no flutter sdk dependency.',
        ),
      );
      checks.add(
        _Check(
          'Windows desktop enabled',
          project.hasWindows,
          project.hasWindows
              ? null
              : 'Run `flutter create --platforms=windows .`',
        ),
      );
      checks.add(_Check('Has windows/runner', project.hasRunner, null));
      checks.add(
        _Check(
          'Morphic already integrated',
          project.hasMorphicMarker,
          project.hasMorphicMarker
              ? 'Fence present — `morphic init` would be a no-op.'
              : 'Not yet integrated — `morphic init` would patch the runner.',
        ),
      );
    }

    final hardFail = checks.any((c) => c.ok == false && c.fatal);

    if (asJson) {
      env.out.writeln(
        jsonEncode({
          'command': 'doctor',
          'ok': !hardFail,
          'checks': [for (final c in checks) c.toJson()],
        }),
      );
    } else {
      env.out.writeln('Morphic doctor\n');
      for (final c in checks) {
        final glyph = c.ok == null ? '?' : (c.ok! ? 'OK' : 'X');
        env.out.writeln(
          '  [$glyph] ${c.label}${c.detail != null ? '  — ${c.detail}' : ''}',
        );
      }
      env.out.writeln();
      env.out.writeln(
        hardFail
            ? 'Some required checks failed. Resolve them before `morphic init`.'
            : 'Environment looks usable. Run `morphic init` for a dry-run plan.',
      );
      env.out.writeln();
      env.out.writeln(
        'Spatial Mode (optional): shaped surfaces, materials, workspace composition.',
      );
      env.out.writeln('  Developer Preview — sign in:  dart run morphic:login');
      env.out.writeln(
        '  Learn more:                   https://www.getmorphic.space/spatial',
      );
    }
    return hardFail ? MorphicExit.precondition : MorphicExit.ok;
  }

  Future<_Check> _checkCommand(
    String exe,
    List<String> args,
    String label,
  ) async {
    try {
      final r = await env.runProcess(exe, args);
      if (r.exitCode == 0) {
        final first = (r.stdout as String).split('\n').first.trim();
        return _Check(label, true, first.isEmpty ? null : first);
      }
      return _Check(label, false, 'exit ${r.exitCode}', fatal: true);
    } on ProcessException {
      return _Check(label, false, 'not found on PATH', fatal: true);
    }
  }

  Future<_Check> _checkBundledAssets() async {
    const label = 'Bundled runtime assets intact';
    try {
      final assetsRoot = await env.resolveAssetsRoot();
      final manifest = RuntimeManifest.loadBundled(assetsRoot);
      final problems = manifest.verifyAssets(assetsRoot);
      if (problems.isEmpty) {
        return _Check(
          label,
          true,
          'runtime ${manifest.runtimeVersion}, ${manifest.all.length} files verified',
        );
      }
      return _Check(
        label,
        false,
        '${problems.length} problem(s), e.g. ${problems.first}',
        fatal: true,
      );
    } on StateError catch (e) {
      return _Check(label, false, e.message, fatal: true);
    }
  }
}

class _Check {
  _Check(this.label, this.ok, this.detail, {this.fatal = false});

  final String label;
  final bool? ok;
  final String? detail;
  final bool fatal;

  Map<String, Object?> toJson() => {
    'label': label,
    'ok': ok,
    'detail': detail,
    'fatal': fatal,
  };
}
