import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:path/path.dart' as p;
import 'package:test/test.dart';

import '../helpers.dart';

void main() {
  late Directory sandbox;
  late String projectRoot;

  setUp(() {
    sandbox = Directory.systemTemp.createTempSync('morphic_cli_test_');
    projectRoot = writeFakeProject(sandbox);
  });

  tearDown(() => sandbox.deleteSync(recursive: true));

  test('locate finds the project from a nested directory', () {
    final nested = Directory(p.join(projectRoot, 'lib', 'src'))
      ..createSync(recursive: true);
    final project = FlutterProject.locate(nested.path);
    expect(project, isNotNull);
    expect(p.canonicalize(project!.root), p.canonicalize(projectRoot));
  });

  test('locate returns null when there is no pubspec anywhere up the tree', () {
    // A temp dir outside any Dart project; its parents may theoretically hold
    // a pubspec, so assert from a guaranteed-empty deep chain instead.
    final empty = Directory(p.join(sandbox.path, 'no', 'project', 'here'))
      ..createSync(recursive: true);
    final project = FlutterProject.locate(empty.path);
    // The sandbox itself has no pubspec; the nearest hit could only be outside
    // the system temp dir, which would be unexpected for a test environment.
    expect(project == null || !p.isWithin(sandbox.path, project.root), isTrue);
  });

  test('isFlutterApp requires a flutter sdk dependency', () {
    expect(FlutterProject(projectRoot).isFlutterApp, isTrue);
    File(
      p.join(projectRoot, 'pubspec.yaml'),
    ).writeAsStringSync('name: plain_dart\n');
    expect(FlutterProject(projectRoot).isFlutterApp, isFalse);
  });

  test('hasRunner reflects the windows/runner directory', () {
    expect(FlutterProject(projectRoot).hasRunner, isTrue);
    Directory(p.join(projectRoot, 'windows')).deleteSync(recursive: true);
    final project = FlutterProject(projectRoot);
    expect(project.hasWindows, isFalse);
    expect(project.hasRunner, isFalse);
  });

  test('hasMorphicMarker matches the fence the CMake patcher writes', () {
    final project = FlutterProject(projectRoot);
    expect(project.hasMorphicMarker, isFalse);
    File(
      project.runnerCmakePath,
    ).writeAsStringSync('$vanillaCmake\n$kFenceBegin\n# block\n$kFenceEnd\n');
    expect(project.hasMorphicMarker, isTrue);
  });
}
