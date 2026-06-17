import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:path/path.dart' as p;
import 'package:test/test.dart';

import '../helpers.dart';

void main() {
  late Directory sandbox;
  late String assetsRoot;
  late RuntimeManifest manifest;

  setUp(() {
    sandbox = Directory.systemTemp.createTempSync('morphic_cli_test_');
    assetsRoot = writeFakeAssets(sandbox);
    manifest = RuntimeManifest.loadBundled(assetsRoot);
  });

  tearDown(() => sandbox.deleteSync(recursive: true));

  group('CMakePatcher', () {
    late String cmakePath;

    setUp(() {
      cmakePath = p.join(sandbox.path, 'CMakeLists.txt');
      File(cmakePath).writeAsStringSync(vanillaCmake);
    });

    test('apply appends a fenced block listing only .cpp sources', () {
      final result = CMakePatcher(cmakePath, manifest).apply();
      expect(result.status, TransformStatus.applied);
      final text = File(cmakePath).readAsStringSync();
      expect(text, startsWith(vanillaCmake));
      expect(text, contains(kFenceBegin));
      expect(text, contains(kFenceEnd));
      expect(text, contains('"morphic_runtime.cpp"'));
      expect(text, isNot(contains('surface_graph.h')));
      expect(text, contains('CXX_STANDARD 20'));
      expect(text, contains('dwmapi.lib'));
    });

    test('apply is idempotent', () {
      CMakePatcher(cmakePath, manifest).apply();
      final second = CMakePatcher(cmakePath, manifest).apply();
      expect(second.status, TransformStatus.skipped);
    });

    test('apply fails on a missing file', () {
      final result =
          CMakePatcher(p.join(sandbox.path, 'nope.txt'), manifest).apply();
      expect(result.status, TransformStatus.failed);
    });

    test('strip restores the original bytes', () {
      CMakePatcher(cmakePath, manifest).apply();
      final result = stripCmakeFence(cmakePath);
      expect(result.status, TransformStatus.applied);
      expect(File(cmakePath).readAsStringSync(), vanillaCmake);
    });

    test('strip without a fence is a skip', () {
      expect(stripCmakeFence(cmakePath).status, TransformStatus.skipped);
    });

    test('apply preserves CRLF line endings', () {
      final crlf = vanillaCmake.replaceAll('\n', '\r\n');
      File(cmakePath).writeAsStringSync(crlf);
      CMakePatcher(cmakePath, manifest).apply();
      final text = File(cmakePath).readAsStringSync();
      expect(
        text.contains(RegExp(r'(?<!\r)\n')),
        isFalse,
        reason: 'no bare LF should be introduced into a CRLF file',
      );
      expect(stripCmakeFence(cmakePath).status, TransformStatus.applied);
      expect(File(cmakePath).readAsStringSync(), crlf);
    });

    // PREMIUM ASSET GATE (Phase 1) — the native tier must not pull in the GPU
    // link stack or define MORPHIC_SPATIAL (there is no compositor_ng/ to build).
    test('native tier omits the spatial GPU stack + MORPHIC_SPATIAL', () {
      CMakePatcher(cmakePath, manifest).apply(); // fixture manifest is native
      final text = File(cmakePath).readAsStringSync();
      expect(text, isNot(contains('MORPHIC_SPATIAL')));
      expect(text, isNot(contains('d3d11.lib')));
      // Shared runtime libs are still emitted for both tiers.
      expect(text, contains('dwmapi.lib'));
      expect(text, contains('dbghelp.lib'));
    });

    test('spatial tier injects MORPHIC_SPATIAL + the GPU stack', () {
      final spatialManifest = RuntimeManifest(
        runtimeVersion: '9.9.9-test',
        spatial: true,
        mainReplacement: manifest.mainReplacement,
        files: [
          ...manifest.files,
          ManifestEntry(
            source: 'windows/runner_morphic/compositor_ng/gpu_context.cpp',
            target: 'windows/runner/compositor_ng/gpu_context.cpp',
            sha256: 'deadbeef',
            kind: ManifestEntry.kindRuntimeSource,
          ),
        ],
      );
      CMakePatcher(cmakePath, spatialManifest).apply();
      final text = File(cmakePath).readAsStringSync();
      expect(text, contains('MORPHIC_SPATIAL'));
      expect(text, contains('d3d11.lib'));
      expect(text, contains('windowsapp.lib'));
      expect(text, contains('"compositor_ng/gpu_context.cpp"'));
    });
  });

  group('PubspecPatcher', () {
    late String pubspecPath;

    setUp(() {
      pubspecPath = p.join(sandbox.path, 'pubspec.yaml');
      File(pubspecPath).writeAsStringSync(vanillaPubspec);
    });

    test('apply inserts the dep directly under dependencies:', () {
      final result =
          PubspecPatcher(
            pubspecPath,
            morphicDepLines: ['  morphic: ^0.1.0'],
          ).apply();
      expect(result.status, TransformStatus.applied);
      final text = File(pubspecPath).readAsStringSync();
      expect(text, contains('dependencies:\n  morphic: ^0.1.0\n  flutter:'));
    });

    test('apply is idempotent', () {
      PubspecPatcher(
        pubspecPath,
        morphicDepLines: ['  morphic: ^0.1.0'],
      ).apply();
      final second =
          PubspecPatcher(
            pubspecPath,
            morphicDepLines: ['  morphic: ^0.1.0'],
          ).apply();
      expect(second.status, TransformStatus.skipped);
    });

    test('apply fails without a dependencies block', () {
      File(
        pubspecPath,
      ).writeAsStringSync('name: x\nflutter:\n  sdk: flutter\n');
      final result =
          PubspecPatcher(
            pubspecPath,
            morphicDepLines: ['  morphic: ^0.1.0'],
          ).apply();
      expect(result.status, TransformStatus.failed);
    });

    test('strip removes a version dep and restores original bytes', () {
      PubspecPatcher(
        pubspecPath,
        morphicDepLines: ['  morphic: ^0.1.0'],
      ).apply();
      final result = stripMorphicDependency(pubspecPath);
      expect(result.status, TransformStatus.applied);
      expect(File(pubspecPath).readAsStringSync(), vanillaPubspec);
    });

    test('strip removes a path-block dep including continuation lines', () {
      PubspecPatcher(
        pubspecPath,
        morphicDepLines: ['  morphic:', '    path: ../morphic'],
      ).apply();
      stripMorphicDependency(pubspecPath);
      final text = File(pubspecPath).readAsStringSync();
      expect(text, vanillaPubspec);
    });

    test('strip does not eat blank lines or following blocks', () {
      PubspecPatcher(
        pubspecPath,
        morphicDepLines: ['  morphic: ^0.1.0'],
      ).apply();
      stripMorphicDependency(pubspecPath);
      final text = File(pubspecPath).readAsStringSync();
      expect(text, contains('\n\ndev_dependencies:'));
    });

    test('round-trips a CRLF pubspec byte-identically', () {
      final crlf = vanillaPubspec.replaceAll('\n', '\r\n');
      File(pubspecPath).writeAsStringSync(crlf);
      PubspecPatcher(
        pubspecPath,
        morphicDepLines: ['  morphic: ^0.1.0'],
      ).apply();
      final patched = File(pubspecPath).readAsStringSync();
      expect(patched.contains(RegExp(r'(?<!\r)\n')), isFalse);
      stripMorphicDependency(pubspecPath);
      expect(File(pubspecPath).readAsStringSync(), crlf);
    });

    test('does not match morphic_cli or other prefixed deps', () {
      File(pubspecPath).writeAsStringSync('''
name: x
dependencies:
  morphic_cli: ^1.0.0
  flutter:
    sdk: flutter
''');
      final result = stripMorphicDependency(pubspecPath);
      expect(result.status, TransformStatus.skipped);
    });
  });
}
