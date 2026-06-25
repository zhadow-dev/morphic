import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:path/path.dart' as p;
import 'package:test/test.dart';

import '../helpers.dart';

void main() {
  late Directory sandbox;
  late String assetsRoot;

  setUp(() {
    sandbox = Directory.systemTemp.createTempSync('morphic_cli_test_');
    assetsRoot = writeFakeAssets(sandbox);
  });

  tearDown(() => sandbox.deleteSync(recursive: true));

  test('loadBundled parses the generated manifest', () {
    final manifest = RuntimeManifest.loadBundled(assetsRoot);
    expect(manifest.runtimeVersion, '9.9.9-test');
    expect(manifest.files, hasLength(2));
    expect(manifest.all, hasLength(3));
    expect(manifest.mainReplacement.kind, ManifestEntry.kindMainReplacement);
    expect(manifest.mainReplacement.target, 'windows/runner/main.cpp');
  });

  test('loadBundled throws a clear error when the manifest is missing', () {
    expect(
      () => RuntimeManifest.loadBundled(sandbox.path),
      throwsA(
        isA<StateError>().having(
          (e) => e.message,
          'message',
          contains('manifest not found'),
        ),
      ),
    );
  });

  test('toJson/fromJson round-trips', () {
    final manifest = RuntimeManifest.loadBundled(assetsRoot);
    final round = RuntimeManifest.fromJson(
      Map<String, dynamic>.from(manifest.toJson()),
    );
    expect(round.runtimeVersion, manifest.runtimeVersion);
    expect(
      round.files.map((e) => e.sha256),
      manifest.files.map((e) => e.sha256),
    );
  });

  test('verifyAssets passes on an intact tree', () {
    final manifest = RuntimeManifest.loadBundled(assetsRoot);
    expect(manifest.verifyAssets(assetsRoot), isEmpty);
  });

  test('verifyAssets reports tampering and missing files', () {
    final manifest = RuntimeManifest.loadBundled(assetsRoot);
    File(
      p.join(assetsRoot, manifest.files.first.source),
    ).writeAsStringSync('tampered');
    File(p.join(assetsRoot, manifest.files.last.source)).deleteSync();
    final problems = manifest.verifyAssets(assetsRoot);
    expect(problems, hasLength(2));
    expect(problems.join('\n'), contains('hash mismatch'));
    expect(problems.join('\n'), contains('missing asset'));
  });
}
