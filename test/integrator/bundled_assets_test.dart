import 'package:morphic/src/integrator/integrator.dart';
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

  // PREMIUM ASSET GATE — the security boundary is DISTRIBUTION: free users never
  // receive the spatial compositor sources. The boundary is enforced by KEEPING
  // compositor_ng/ OUT OF THE PUBLIC TREE entirely (its real delivery is premium/
  // roadmap — fetched only after license activation). So in this repo BOTH tiers
  // must be compositor_ng-free; there is no public spatial-source leakage.
  test('public tree leaks no compositor_ng sources, in any tier', () async {
    final assetsRoot = await findRuntimeAssetsRoot();
    for (final spatial in [false, true]) {
      final m = RuntimeManifest.loadBundled(assetsRoot, spatial: spatial);
      expect(
        m.files.where((e) => e.target.contains('/compositor_ng/')),
        isEmpty,
        reason: 'no public spatial-source leakage (compositor_ng is premium)',
      );
      expect(m.verifyAssets(assetsRoot), isEmpty);
    }
    final native = RuntimeManifest.loadBundled(assetsRoot, spatial: false);
    expect(native.spatial, isFalse);
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
