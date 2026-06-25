import 'dart:io';

import 'package:args/command_runner.dart';
import 'package:morphic/src/integrator/integrator.dart';

/// `dart run morphic:remove [...]` — colon-form alias for `dart run morphic
/// remove`. Reverses a Morphic install, restoring the project byte-for-byte.
Future<void> main(List<String> args) async {
  try {
    exit(await buildMorphicRunner().run(['remove', ...args]) ?? MorphicExit.ok);
  } on UsageException catch (e) {
    stderr.writeln(e);
    exit(MorphicExit.usage);
  } catch (e) {
    stderr.writeln('morphic: $e');
    exit(MorphicExit.refused);
  }
}
