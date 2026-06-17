import 'dart:io';

import 'package:args/command_runner.dart';
import 'package:morphic/src/integrator/integrator.dart';

/// `dart run morphic <command>` — Morphic's integrator tooling (init / remove /
/// doctor). The runtime sources ship as package assets and are materialized into
/// your project's windows/runner by `init` (idempotent, reversible).
Future<void> main(List<String> args) async {
  try {
    exit(await buildMorphicRunner().run(args) ?? MorphicExit.ok);
  } on UsageException catch (e) {
    stderr.writeln(e);
    exit(MorphicExit.usage);
  } catch (e) {
    stderr.writeln('morphic: $e');
    exit(MorphicExit.refused);
  }
}
