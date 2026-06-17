import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:path/path.dart' as p;
import 'package:test/test.dart';

import '../helpers.dart';

void main() {
  late Directory sandbox;
  late String assetsRoot;
  late String projectRoot;
  late RuntimeManifest manifest;
  late InstallEngine engine;

  setUp(() {
    sandbox = Directory.systemTemp.createTempSync('morphic_cli_test_');
    assetsRoot = writeFakeAssets(sandbox);
    projectRoot = writeFakeProject(sandbox);
    manifest = RuntimeManifest.loadBundled(assetsRoot);
    engine = InstallEngine(projectRoot, assetsRoot: assetsRoot);
  });

  tearDown(() => sandbox.deleteSync(recursive: true));

  InstallRecord performInstall() {
    final backups = [
      engine.backup('windows/runner/main.cpp'),
      engine.backup('windows/runner/CMakeLists.txt'),
      engine.backup('pubspec.yaml'),
    ];
    final materialized = [
      for (final entry in manifest.all) engine.materialize(entry),
    ];
    final record = InstallRecord(
      runtimeVersion: manifest.runtimeVersion,
      installedAt: DateTime.now().toUtc().toIso8601String(),
      materialized: materialized,
      backups: backups,
    );
    engine.writeRecord(record);
    return record;
  }

  test('backup copies existing files and records their hash', () {
    final entry = engine.backup('windows/runner/main.cpp');
    expect(entry.existedBefore, isTrue);
    expect(entry.sha256, isNotNull);
    expect(
      File(
        p.join(engine.backupDir, 'windows', 'runner', 'main.cpp'),
      ).readAsStringSync(),
      vanillaMainCpp,
    );
  });

  test('backup of a missing file records a created marker', () {
    final entry = engine.backup('windows/runner/not_there.cpp');
    expect(entry.existedBefore, isFalse);
    expect(entry.sha256, isNull);
  });

  test('materialize verifies the source hash before copying', () {
    final entry = manifest.files.first;
    File(p.join(assetsRoot, entry.source)).writeAsStringSync('corrupted');
    expect(() => engine.materialize(entry), throwsStateError);
    expect(projectFileExists(projectRoot, entry.target), isFalse);
  });

  test('materialize without an assetsRoot throws', () {
    final bare = InstallEngine(projectRoot);
    expect(() => bare.materialize(manifest.files.first), throwsStateError);
  });

  test('install + reverse restores the project byte-identically', () {
    performInstall();
    expect(
      projectFileExists(projectRoot, 'windows/runner/morphic_runtime.cpp'),
      isTrue,
    );
    expect(
      readProjectFile(projectRoot, 'windows/runner/main.cpp'),
      fakeMainReplacement,
    );

    final actions = engine.reverse();
    expect(actions, contains('restored windows/runner/main.cpp'));
    expect(
      readProjectFile(projectRoot, 'windows/runner/main.cpp'),
      vanillaMainCpp,
    );
    expect(
      projectFileExists(projectRoot, 'windows/runner/morphic_runtime.cpp'),
      isFalse,
    );
    expect(Directory(engine.morphicDir).existsSync(), isFalse);
  });

  test('reverse prunes emptied runtime dirs but keeps runner/', () {
    performInstall();
    expect(
      Directory(
        p.join(projectRoot, 'windows', 'runner', 'multisurface'),
      ).existsSync(),
      isTrue,
    );
    engine.reverse();
    expect(
      Directory(
        p.join(projectRoot, 'windows', 'runner', 'multisurface'),
      ).existsSync(),
      isFalse,
    );
    expect(
      Directory(p.join(projectRoot, 'windows', 'runner')).existsSync(),
      isTrue,
    );
  });

  test('reverse with no record is a safe no-op', () {
    expect(engine.reverse(), ['nothing to reverse (no install record).']);
  });

  test('record round-trips through JSON', () {
    final record = performInstall();
    final read = engine.readRecord()!;
    expect(read.runtimeVersion, record.runtimeVersion);
    expect(read.materializedTargets, record.materializedTargets);
    expect(read.backups.length, record.backups.length);
    expect(read.backups.first.existedBefore, isTrue);
  });

  test('detectDrift reports only hand-edited materialized files', () {
    performInstall();
    expect(engine.detectDrift(), isEmpty);
    File(
      p.join(projectRoot, 'windows', 'runner', 'morphic_runtime.cpp'),
    ).writeAsStringSync('// edited by hand');
    expect(engine.detectDrift(), ['windows/runner/morphic_runtime.cpp']);
  });
}
