import 'package:args/command_runner.dart';

import 'cli_environment.dart';
import 'doctor_command.dart';
import 'init_command.dart';
import 'remove_command.dart';

/// Builds the `morphic` command runner. [environment] defaults to the real
/// process environment; tests inject a sandboxed one.
CommandRunner<int> buildMorphicRunner({CliEnvironment? environment}) {
  final env = environment ?? CliEnvironment();
  return CommandRunner<int>(
      'morphic',
      'Install and manage the Morphic Windows runtime in a Flutter project.',
    )
    ..addCommand(DoctorCommand(env))
    ..addCommand(InitCommand(env))
    ..addCommand(RemoveCommand(env));
}
