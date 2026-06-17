import 'dart:io';

import 'package:args/command_runner.dart';
import 'package:morphic/src/integrator/integrator.dart';

/// `dart run morphic:init [...]` — colon-form alias for `dart run morphic init`.
/// Transforms this Flutter project's Windows runner into a Morphic-hosted
/// runtime (idempotent, reversible). Add `--apply` to perform it.
Future<void> main(List<String> args) async {
  try {
    exit(await buildMorphicRunner().run(['init', ...args]) ?? MorphicExit.ok);
  } on UsageException catch (e) {
    stderr.writeln(e);
    exit(MorphicExit.usage);
  } catch (e) {
    stderr.writeln('morphic: $e');
    exit(MorphicExit.refused);
  }
}
