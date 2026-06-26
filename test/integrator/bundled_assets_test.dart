import 'dart:io';

import 'package:morphic/src/integrator/integrator.dart';
import 'package:path/path.dart' as p;
import 'package:test/test.dart';

/// Guards the shipped state of the package itself: every runtime asset must
/// exist and match the hash recorded in the generated manifest. Fails when
/// someone edits an asset without re-running `dart run tool/gen_manifest.dart`.
void main() {
  test('bundled runtime_assets match the generated manifest', () async {
    final assetsRoot = await findRuntimeAssetsRoot();
    final manifest = RuntimeManifest.loadBundled(assetsRoot);

    expect(manifest.files, isNotEmpty);
    expect(manifest.mainReplacement.target, 'windows/runner/main.cpp');
    expect(manifest.verifyAssets(assetsRoot), isEmpty);
  });

  test('manifest targets stay inside windows/runner/', () async {
    final assetsRoot = await findRuntimeAssetsRoot();
    final manifest = RuntimeManifest.loadBundled(assetsRoot);
    for (final entry in manifest.all) {
      expect(
        entry.target,
        startsWith('windows/runner/'),
        reason: 'init must never write outside the Windows runner',
      );
      expect(
        entry.target,
        isNot(contains('..')),
        reason: 'no path traversal in manifest targets',
      );
    }
  });

  // PREMIUM ASSET GATE — the security boundary is DISTRIBUTION. The NATIVE
  // manifest (what ships in the free package) must be compositor_ng-free. The
  // SPATIAL manifest is a strict SUPERSET that INCLUDES compositor_ng — it is
  // delivered only in the license-gated ZIP, never in the published package, so
  // it is intentionally ABSENT from the premium-stripped public tree.
  test('native tier is compositor_ng-free; spatial tier is a superset that '
      'includes it', () async {
    final assetsRoot = await findRuntimeAssetsRoot();

    // NATIVE: always present; never carries premium spatial sources.
    final native = RuntimeManifest.loadBundled(assetsRoot, spatial: false);
    expect(native.spatial, isFalse);
    expect(
      native.files.where((e) => e.target.contains('/compositor_ng/')),
      isEmpty,
      reason: 'the free/native package must never carry premium spatial sources',
    );
    expect(native.verifyAssets(assetsRoot), isEmpty);

    // SPATIAL: present in the private repo + the delivered ZIP, but ABSENT from
    // the premium-stripped public tree. When present it must include
    // compositor_ng, hash-match the tree, and be a strict superset of native.
    if (!File(p.join(assetsRoot, 'manifest_spatial.json')).existsSync()) {
      return; // public tier — nothing further to assert
    }
    final spatial = RuntimeManifest.loadBundled(assetsRoot, spatial: true);
    expect(spatial.spatial, isTrue);
    expect(
      spatial.files.where((e) => e.target.contains('/compositor_ng/')),
      isNotEmpty,
      reason: 'the spatial tier must include the compositor_ng sources it needs',
    );
    expect(spatial.verifyAssets(assetsRoot), isEmpty);

    final nativeTargets = native.files.map((e) => e.target).toSet();
    final spatialTargets = spatial.files.map((e) => e.target).toSet();
    expect(
      nativeTargets.difference(spatialTargets),
      isEmpty,
      reason: 'spatial must be a strict superset of native',
    );
    expect(spatialTargets.length, greaterThan(nativeTargets.length));
  });

  // The entrypoint and CMakeLists are special-cased (template + patcher); a
  // stray copy under runner_morphic/ must never leak in as a runtime source.
  test('neither tier materializes a stray main.cpp / CMakeLists.txt', () async {
    final assetsRoot = await findRuntimeAssetsRoot();
    for (final spatial in [false, true]) {
      final m = RuntimeManifest.loadBundled(assetsRoot, spatial: spatial);
      expect(
        m.files.where(
          (e) =>
              e.target == 'windows/runner/main.cpp' ||
              e.target.endsWith('/CMakeLists.txt'),
        ),
        isEmpty,
        reason: 'runtime sources must exclude the specially-handled files',
      );
    }
  });
}
