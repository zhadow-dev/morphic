import 'package:args/command_runner.dart';

import 'cli_environment.dart';
import 'doctor_command.dart';
import 'init_command.dart';
import 'remove_command.dart';

/// Builds the `morphic` command runner. [environment] defaults to the real
/// process environment; tests inject a sandboxed one.
CommandRunner<int> buildMorphicRunner({CliEnvironment? environment}) {
  final env = environment ?? CliEnvironment();
  return _MorphicRunner()
    ..addCommand(DoctorCommand(env))
    ..addCommand(InitCommand(env))
    ..addCommand(RemoveCommand(env));
}

/// CommandRunner with a Spatial-discovery footer on `morphic --help`.
class _MorphicRunner extends CommandRunner<int> {
  _MorphicRunner()
    : super(
        'morphic',
        'Install and manage the Morphic Windows runtime in a Flutter project.',
      );

  @override
  String get usageFooter =>
      '\nSpatial Mode (optional): shaped surfaces, materials, workspace composition.\n'
      '  Sign in:    dart run morphic:login   (free Developer Preview)\n'
      '  Learn more: https://www.getmorphic.space/spatial';
}
