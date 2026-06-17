import 'dart:convert';
import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:test/test.dart';

import '../helpers.dart';

void main() {
  late Directory sandbox;
  late String assetsRoot;
  late String projectRoot;
  late SandboxCli cli;

  setUp(() {
    sandbox = Directory.systemTemp.createTempSync('morphic_cli_test_');
    assetsRoot = writeFakeAssets(sandbox);
    projectRoot = writeFakeProject(sandbox);
    cli = SandboxCli(projectRoot: projectRoot, assetsRoot: assetsRoot);
  });

  tearDown(() => sandbox.deleteSync(recursive: true));

  List<Map<String, dynamic>> checksOf(String output) {
    final report = jsonDecode(output.trim()) as Map<String, dynamic>;
    return (report['checks'] as List).cast<Map<String, dynamic>>();
  }

  test('doctor --json reports a healthy sandbox project', () async {
    final code = await cli.run(['doctor', '--json']);
    expect(code, MorphicExit.ok);

    final checks = checksOf(cli.out.toString());
    Map<String, dynamic> check(String label) =>
        checks.singleWhere((c) => c['label'] == label);

    expect(check('Flutter SDK')['ok'], isTrue);
    expect(check('Flutter project found')['ok'], isTrue);
    expect(check('Is a Flutter app')['ok'], isTrue);
    expect(check('Has windows/runner')['ok'], isTrue);
    expect(check('Bundled runtime assets intact')['ok'], isTrue);
    expect(check('Morphic already integrated')['ok'], isFalse);
  });

  test('doctor sees the integration marker after init', () async {
    await cli.run(['init', '--apply']);
    cli.out.clear();
    await cli.run(['doctor', '--json']);
    final checks = checksOf(cli.out.toString());
    final integrated = checks.singleWhere(
      (c) => c['label'] == 'Morphic already integrated',
    );
    expect(integrated['ok'], isTrue);
  });

  test(
    'doctor fails hard when a required toolchain command is missing',
    () async {
      final out = StringBuffer();
      final env = CliEnvironment(
        workingDirectory: projectRoot,
        variables: const {},
        out: out,
        err: StringBuffer(),
        assetsRoot: assetsRoot,
        runProcess:
            (exe, args) async => throw ProcessException(exe, args, 'not found'),
      );
      final code =
          await buildMorphicRunner(
            environment: env,
          ).run(['doctor', '--json']) ??
          0;
      expect(code, MorphicExit.precondition);
      final report = jsonDecode(out.toString().trim()) as Map<String, dynamic>;
      expect(report['ok'], isFalse);
    },
  );

  test('doctor flags corrupted bundled assets', () async {
    final manifest = RuntimeManifest.loadBundled(assetsRoot);
    File(
      '$assetsRoot/${manifest.files.first.source}',
    ).writeAsStringSync('tampered');
    final code = await cli.run(['doctor', '--json']);
    expect(code, MorphicExit.precondition);
    final checks = checksOf(cli.out.toString());
    final assets = checks.singleWhere(
      (c) => c['label'] == 'Bundled runtime assets intact',
    );
    expect(assets['ok'], isFalse);
    expect(assets['detail'], contains('hash mismatch'));
  });

  test('doctor in plain text mode prints OK/X glyph lines', () async {
    final code = await cli.run(['doctor']);
    expect(code, MorphicExit.ok);
    final text = cli.out.toString();
    expect(text, contains('Morphic doctor'));
    expect(text, contains('[OK] Flutter project found'));
  });
}
