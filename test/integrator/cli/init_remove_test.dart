import 'dart:convert';
import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:path/path.dart' as p;
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

  Map<String, String> snapshotProject() {
    final files = <String, String>{};
    for (final f
        in Directory(projectRoot).listSync(recursive: true).whereType<File>()) {
      files[p.relative(f.path, from: projectRoot).replaceAll('\\', '/')] =
          f.readAsStringSync();
    }
    return files;
  }

  test('init without --apply is a pure dry run', () async {
    final before = snapshotProject();
    final code = await cli.run(['init']);
    expect(code, MorphicExit.ok);
    expect(cli.out.toString(), contains('DRY RUN'));
    expect(snapshotProject(), before);
  });

  test('init --apply materializes, patches, and records', () async {
    final code = await cli.run(['init', '--apply']);
    expect(code, MorphicExit.ok);

    expect(
      readProjectFile(projectRoot, 'windows/runner/main.cpp'),
      fakeMainReplacement,
    );
    expect(
      projectFileExists(projectRoot, 'windows/runner/morphic_runtime.cpp'),
      isTrue,
    );
    expect(
      projectFileExists(
        projectRoot,
        'windows/runner/multisurface/surface_graph.h',
      ),
      isTrue,
    );
    expect(
      readProjectFile(projectRoot, 'windows/runner/CMakeLists.txt'),
      contains(kFenceBegin),
    );
    expect(
      readProjectFile(projectRoot, 'pubspec.yaml'),
      contains('morphic: ^0.1.0'),
    );
    expect(projectFileExists(projectRoot, '.morphic/install.json'), isTrue);

    // The integration marker the doctor checks must match what was written.
    expect(FlutterProject(projectRoot).hasMorphicMarker, isTrue);
  });

  test('init --apply twice is idempotent', () async {
    await cli.run(['init', '--apply']);
    final after = snapshotProject();
    final code = await cli.run(['init', '--apply']);
    expect(code, MorphicExit.ok);
    expect(cli.out.toString(), contains('already installed'));
    expect(snapshotProject(), after);
  });

  // Rewrites the install record's runtime ABI to [abi] — simulating a project
  // installed with an older runtime, so a re-init must detect the mismatch.
  void setInstalledRuntimeAbi(String abi) {
    final recFile = File(p.join(projectRoot, '.morphic', 'install.json'));
    final rec = jsonDecode(recFile.readAsStringSync()) as Map<String, dynamic>;
    rec['runtimeVersion'] = abi;
    recFile.writeAsStringSync(jsonEncode(rec));
  }

  test('re-init on a MATCHING runtime says nothing to do (version-aware)',
      () async {
    await cli.run(['init', '--apply']);
    cli.out.clear();
    final code = await cli.run(['init']);
    expect(code, MorphicExit.ok);
    final out = cli.out.toString();
    expect(out, contains('Runtime already installed'));
    expect(out, contains('9.9.9-test')); // the packaged ABI
    expect(out, contains('Nothing to do'));
    expect(out, isNot(contains('upgrade is available')));
  });

  test('re-init on an OLDER runtime reports an upgrade is available', () async {
    await cli.run(['init', '--apply']);
    setInstalledRuntimeAbi('0.1.0'); // older than the packaged 9.9.9-test
    cli.out.clear();
    final code = await cli.run(['init']);
    expect(code, MorphicExit.ok);
    final out = cli.out.toString();
    expect(out, contains('A runtime upgrade is available'));
    expect(out, contains('0.1.0')); // installed
    expect(out, contains('9.9.9-test')); // available
    expect(out, contains('--force'));
    expect(out, isNot(contains('Nothing to do')));
  });

  test('re-init --json flags upgradeAvailable on an ABI mismatch', () async {
    await cli.run(['init', '--apply']);
    setInstalledRuntimeAbi('0.1.0');
    cli.out.clear();
    final code = await cli.run(['init', '--json']);
    expect(code, MorphicExit.ok);
    final payload = jsonDecode(cli.out.toString()) as Map<String, dynamic>;
    expect(payload['alreadyInstalled'], isTrue);
    expect(payload['installedRuntimeVersion'], '0.1.0');
    expect(payload['packagedRuntimeVersion'], '9.9.9-test');
    expect(payload['upgradeAvailable'], isTrue);
  });

  test('init honors MORPHIC_LOCAL_REPO with a path dependency', () async {
    final repo = Directory(p.join(sandbox.path, 'morphic_repo'))
      ..createSync(recursive: true);
    final out = StringBuffer();
    final env = CliEnvironment(
      workingDirectory: projectRoot,
      variables: {'MORPHIC_LOCAL_REPO': repo.path},
      out: out,
      err: StringBuffer(),
      assetsRoot: assetsRoot,
    );
    final code =
        await buildMorphicRunner(environment: env).run(['init', '--apply']) ??
        0;
    expect(code, MorphicExit.ok);
    final pubspec = readProjectFile(projectRoot, 'pubspec.yaml');
    expect(pubspec, contains('morphic:'));
    expect(pubspec, contains('path: ${repo.path.replaceAll(r'\', '/')}'));
  });

  test('remove --apply restores the project byte-identically', () async {
    final before = snapshotProject();
    await cli.run(['init', '--apply']);
    final code = await cli.run(['remove', '--apply']);
    expect(code, MorphicExit.ok);
    expect(snapshotProject(), before);
  });

  test('remove refuses on drift without --force, proceeds with it', () async {
    await cli.run(['init', '--apply']);
    File(
      p.join(projectRoot, 'windows', 'runner', 'morphic_runtime.cpp'),
    ).writeAsStringSync('// hand edit');

    final refused = await cli.run(['remove', '--apply']);
    expect(refused, MorphicExit.refused);
    expect(projectFileExists(projectRoot, '.morphic/install.json'), isTrue);

    final forced = await cli.run(['remove', '--apply', '--force']);
    expect(forced, MorphicExit.ok);
    expect(projectFileExists(projectRoot, '.morphic/install.json'), isFalse);
    expect(
      readProjectFile(projectRoot, 'windows/runner/main.cpp'),
      vanillaMainCpp,
    );
  });

  test('remove when not installed is a no-op', () async {
    final code = await cli.run(['remove', '--apply']);
    expect(code, MorphicExit.ok);
    expect(cli.out.toString(), contains('not installed'));
  });

  test('REGRESSION: init --force preserves the original backup, '
      'so a later remove restores pristine files', () async {
    await cli.run(['init', '--apply']);
    final code = await cli.run(['init', '--apply', '--force']);
    expect(code, MorphicExit.ok);

    // The backup must still hold the user's ORIGINAL main.cpp, not Morphic's
    // replacement that a naive re-backup would have captured.
    final backedUp =
        File(
          p.join(
            projectRoot,
            '.morphic',
            'backup',
            'windows',
            'runner',
            'main.cpp',
          ),
        ).readAsStringSync();
    expect(backedUp, vanillaMainCpp);

    await cli.run(['remove', '--apply']);
    expect(
      readProjectFile(projectRoot, 'windows/runner/main.cpp'),
      vanillaMainCpp,
    );
    expect(
      readProjectFile(projectRoot, 'pubspec.yaml'),
      isNot(contains('morphic:')),
    );
    expect(
      readProjectFile(projectRoot, 'windows/runner/CMakeLists.txt'),
      isNot(contains(kFenceBegin)),
    );
  });

  test('REGRESSION: a mid-install failure rolls the project back '
      'byte-identically', () async {
    // No `dependencies:` block → the pubspec patch (a late install step)
    // fails after materialization already happened.
    const brokenPubspec = '''
name: rollback_app

flutter:
  sdk: flutter
''';
    final root = writeFakeProject(
      sandbox,
      name: 'rollback_app',
      pubspec: brokenPubspec,
    );
    final broken = SandboxCli(projectRoot: root, assetsRoot: assetsRoot);

    final before = <String, String>{
      for (final f
          in Directory(root).listSync(recursive: true).whereType<File>())
        p.relative(f.path, from: root).replaceAll('\\', '/'):
            f.readAsStringSync(),
    };

    final code = await broken.run(['init', '--apply']);
    expect(code, MorphicExit.refused);
    expect(broken.err.toString(), contains('pubspec patch failed'));

    final after = <String, String>{
      for (final f
          in Directory(root).listSync(recursive: true).whereType<File>())
        p.relative(f.path, from: root).replaceAll('\\', '/'):
            f.readAsStringSync(),
    };
    expect(after, before);
  });

  test('init not in a Flutter app fails with a precondition error', () async {
    File(
      p.join(projectRoot, 'pubspec.yaml'),
    ).writeAsStringSync('name: plain_dart\n');
    final code = await cli.run(['init', '--apply']);
    expect(code, MorphicExit.precondition);
    expect(cli.err.toString(), contains('not a Flutter app'));
  });

  test('init --json emits a machine-readable dry-run plan', () async {
    final code = await cli.run(['init', '--json']);
    expect(code, MorphicExit.ok);
    final report =
        jsonDecode(cli.out.toString().trim()) as Map<String, dynamic>;
    expect(report['command'], 'init');
    expect(report['ok'], isTrue);
    expect(report['mode'], 'dry-run');
    expect(report['plan'], isA<List<dynamic>>());
  });

  test('remove --json reports drift in the dry-run payload', () async {
    await cli.run(['init', '--apply']);
    File(
      p.join(projectRoot, 'windows', 'runner', 'morphic_runtime.cpp'),
    ).writeAsStringSync('// hand edit');
    cli.out.clear();
    final code = await cli.run(['remove', '--json']);
    expect(code, MorphicExit.ok);
    final report =
        jsonDecode(cli.out.toString().trim()) as Map<String, dynamic>;
    expect(report['drift'], ['windows/runner/morphic_runtime.cpp']);
  });
}
