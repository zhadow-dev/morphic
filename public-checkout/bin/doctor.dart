import 'dart:io';

import 'package:args/command_runner.dart';
import 'package:morphic/src/integrator/integrator.dart';

/// `dart run morphic:doctor [...]` — colon-form alias for `dart run morphic
/// doctor`. Read-only diagnostics of the environment + project integration.
Future<void> main(List<String> args) async {
  try {
    exit(await buildMorphicRunner().run(['doctor', ...args]) ?? MorphicExit.ok);
  } on UsageException catch (e) {
    stderr.writeln(e);
    exit(MorphicExit.usage);
  } catch (e) {
    stderr.writeln('morphic: $e');
    exit(MorphicExit.refused);
  }
}
