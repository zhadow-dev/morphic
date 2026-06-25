import 'dart:io';

import '../core/assets.dart';

typedef ProcessRunner =
    Future<ProcessResult> Function(String executable, List<String> arguments);

Future<ProcessResult> _defaultRunProcess(
  String executable,
  List<String> arguments,
) => Process.run(executable, arguments, runInShell: true);

/// Everything a command needs from the outside world, in one injectable seam:
/// working directory, environment variables, output sinks, asset location,
/// and process execution. Production code uses the defaults; tests construct
/// one pointing at a sandbox.
class CliEnvironment {
  CliEnvironment({
    String? workingDirectory,
    Map<String, String>? variables,
    StringSink? out,
    StringSink? err,
    String? assetsRoot,
    ProcessRunner? runProcess,
  }) : workingDirectory = workingDirectory ?? Directory.current.path,
       variables = variables ?? Platform.environment,
       out = out ?? stdout,
       err = err ?? stderr,
       runProcess = runProcess ?? _defaultRunProcess,
       _assetsRoot = assetsRoot;

  final String workingDirectory;
  final Map<String, String> variables;
  final StringSink out;
  final StringSink err;
  final ProcessRunner runProcess;

  String? _assetsRoot;

  /// The package's `runtime_assets/` directory (resolved once, cached).
  Future<String> resolveAssetsRoot() async =>
      _assetsRoot ??= await findRuntimeAssetsRoot();
}

/// Exit codes shared by all commands.
abstract final class MorphicExit {
  static const int ok = 0;

  /// A precondition failed (not a Flutter app, missing runner, …).
  static const int precondition = 1;

  /// The command refused to proceed or failed and rolled back.
  static const int refused = 2;

  /// Bad invocation (unknown command/flag).
  static const int usage = 64;
}
