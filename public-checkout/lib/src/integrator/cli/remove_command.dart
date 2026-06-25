import 'dart:convert';

import 'package:args/command_runner.dart';

import '../core/install_engine.dart';
import '../core/project.dart';
import '../core/transforms.dart';
import 'cli_environment.dart';

/// `morphic remove` — cleanly reverse `morphic init`.
///
/// Restores the backed-up main.cpp / CMakeLists.txt / pubspec, deletes the
/// materialized runtime sources, strips the fenced blocks, and removes
/// `.morphic/`. Drift detection refuses to clobber hand-edited runtime files
/// unless `--force`.
class RemoveCommand extends Command<int> {
  RemoveCommand(this.env) {
    argParser
      ..addFlag(
        'apply',
        negatable: false,
        help: 'Perform the removal. Without this, prints a plan only.',
      )
      ..addFlag(
        'force',
        negatable: false,
        help: 'Remove even if materialized files were hand-edited (drift).',
      )
      ..addFlag(
        'json',
        negatable: false,
        help: 'Emit the plan/result as JSON.',
      );
  }

  final CliEnvironment env;

  @override
  final String name = 'remove';
  @override
  final String description =
      'Reverse the Morphic transform on this project (Windows).';

  @override
  Future<int> run() async {
    final apply = argResults!.flag('apply');
    final force = argResults!.flag('force');
    final asJson = argResults!.flag('json');

    final project = FlutterProject.locate(env.workingDirectory);
    if (project == null) {
      if (asJson) {
        env.out.writeln(
          jsonEncode({
            'command': 'remove',
            'ok': false,
            'error': 'No Flutter project found.',
          }),
        );
      } else {
        env.err.writeln('No Flutter project found.');
      }
      return MorphicExit.precondition;
    }

    // Reversal needs only the install record — not the bundled assets — so it
    // works even from a different morphic version than the one installed.
    final engine = InstallEngine(project.root);
    if (!engine.isInstalled) {
      if (asJson) {
        env.out.writeln(
          jsonEncode({'command': 'remove', 'ok': true, 'installed': false}),
        );
      } else {
        env.out.writeln(
          'Morphic is not installed here (no .morphic/install.json). Nothing to remove.',
        );
      }
      return MorphicExit.ok;
    }

    final rec = engine.readRecord()!;
    final drift = engine.detectDrift();

    if (!asJson) {
      env.out.writeln(
        'morphic remove — ${apply ? 'APPLY' : 'DRY RUN'} for ${project.root}',
      );
      env.out.writeln(
        '  installed runtime: ${rec.runtimeVersion} (at ${rec.installedAt})',
      );
      env.out.writeln(
        '  • delete ${rec.materialized.length} materialized files from windows/runner/',
      );
      env.out.writeln(
        '  • strip fenced blocks from CMakeLists.txt + pubspec.yaml',
      );
      env.out.writeln(
        '  • restore backed-up main.cpp / CMakeLists.txt / pubspec.yaml',
      );
      env.out.writeln('  • remove .morphic/');
      if (drift.isNotEmpty) {
        env.out.writeln(
          '\n  DRIFT: ${drift.length} materialized file(s) were edited since install:',
        );
        for (final d in drift.take(10)) {
          env.out.writeln('      $d');
        }
        if (!force) {
          env.out.writeln(
            '  These edits will be LOST on removal. Re-run with --force to proceed.',
          );
        }
      }
      env.out.writeln();
    }

    if (!apply) {
      if (asJson) {
        env.out.writeln(
          jsonEncode({
            'command': 'remove',
            'ok': true,
            'mode': 'dry-run',
            'installedRuntimeVersion': rec.runtimeVersion,
            'materializedCount': rec.materialized.length,
            'drift': drift,
          }),
        );
      } else {
        env.out.writeln('Dry run only. Re-run with --apply to remove.');
      }
      return MorphicExit.ok;
    }
    if (drift.isNotEmpty && !force) {
      const msg =
          'Refusing to remove: hand-edited runtime files would be lost. Use --force.';
      if (asJson) {
        env.out.writeln(
          jsonEncode({
            'command': 'remove',
            'ok': false,
            'error': msg,
            'drift': drift,
          }),
        );
      } else {
        env.err.writeln(msg);
      }
      return MorphicExit.refused;
    }

    // Strip fences first; the backup restore below supersedes for files that
    // were backed up, but stripping is the correct path when no backup exists.
    stripCmakeFence(project.runnerCmakePath);
    stripMorphicDependency(project.pubspecPath);

    final actions = engine.reverse();
    if (asJson) {
      env.out.writeln(
        jsonEncode({
          'command': 'remove',
          'ok': true,
          'mode': 'apply',
          'actions': actions,
        }),
      );
    } else {
      for (final a in actions) {
        env.out.writeln('  $a');
      }
      env.out.writeln(
        '\nDone. Project restored to its pre-Morphic state. Run `flutter pub get`.',
      );
    }
    return MorphicExit.ok;
  }
}
