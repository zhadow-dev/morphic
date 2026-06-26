// Secure spatial-runtime delivery (internal, pure Dart).
//
// This is the real product infrastructure: authenticated, authorized,
// signature-verified delivery of the premium runtime. Payments later are just
// a flag on the backend — this pipeline does not change.
import 'dart:io';

import 'package:archive/archive_io.dart';
import 'package:crypto/crypto.dart';
import 'package:path/path.dart' as p;

import 'api_client.dart';
import 'auth_store.dart';

/// Outcome of a delivery attempt.
class DeliveryResult {
  DeliveryResult({
    required this.ok,
    required this.message,
    this.version,
    this.installedTo,
    this.assetsRoot,
  });
  final bool ok;
  final String message;
  final String? version;
  final String? installedTo;

  /// The extracted `runtime_assets/` root of the delivered artifact — what
  /// `init --spatial` materializes from. The spatial sources (`compositor_ng/`)
  /// live ONLY here, never in the published package.
  final String? assetsRoot;
}

/// A line-oriented progress sink so the CLI controls presentation.
typedef Log = void Function(String line);

class SpatialDelivery {
  SpatialDelivery({required this.api, required this.store, required this.log});
  final MorphicApi api;
  final AuthStore store;
  final Log log;

  /// Full pipeline: auth → authorize → signed URL → download → verify → install.
  /// Returns a result; never throws for expected conditions (not logged in,
  /// not authorized) — those are reported via [DeliveryResult].
  Future<DeliveryResult> ensure({required String projectRoot}) async {
    // 1. Auth
    final auth = store.read();
    if (auth == null) {
      return DeliveryResult(
        ok: false,
        message: 'Not logged in.\n\n  Run:  dart run morphic:login\n',
      );
    }
    log('  ✓ signed in as ${auth.email}');

    final String accessToken;
    try {
      accessToken = await api.accessToken(auth.refreshToken);
    } on MorphicApiException catch (e) {
      return DeliveryResult(
        ok: false,
        message: 'Session expired ($e).\n\n  Run:  dart run morphic:login\n',
      );
    }

    // 2. Authorize (the one flag)
    final authorized = await api.spatialAccess(accessToken);
    if (!authorized) {
      return DeliveryResult(
        ok: false,
        message: 'Your plan does not include Spatial access.',
      );
    }
    log('  ✓ spatial access authorized');

    // 3. Signed URL
    final signed = await api.spatialDownload(accessToken);
    log('  ✓ resolved spatial runtime v${signed.version}');

    // 4. Download
    final bytes = await api.download(signed.url);
    log('  ✓ downloaded ${bytes.length} bytes');

    // 5. Verify signature/hash
    final actual = sha256.convert(bytes).toString();
    if (actual != signed.sha256) {
      return DeliveryResult(
        ok: false,
        message:
            'Integrity check FAILED.\n  expected: ${signed.sha256}\n  actual:   $actual',
      );
    }
    log('  ✓ sha256 verified');

    // 6. Cache the verified artifact in the project.
    final cacheDir = Directory(p.join(projectRoot, '.morphic', 'spatial'));
    cacheDir.createSync(recursive: true);
    final dest = File(p.join(cacheDir.path, 'runtime-v${signed.version}.zip'));
    dest.writeAsBytesSync(bytes);
    log('  ✓ cached to ${p.relative(dest.path, from: projectRoot)}');

    // 7. Extract it so `init` can materialize the spatial runtime FROM the
    //    artifact. This is the source of truth for the spatial tier: the
    //    compositor_ng/ sources ship only here (the published package is
    //    premium-stripped), so init must read from the extracted tree, not the
    //    bundled package assets.
    final extractDir = Directory(
      p.join(cacheDir.path, 'runtime-v${signed.version}'),
    );
    if (extractDir.existsSync()) extractDir.deleteSync(recursive: true);
    extractDir.createSync(recursive: true);
    await extractFileToDisk(dest.path, extractDir.path);
    // The artifact is a zip OF `runtime_assets/`, so the manifests + sources
    // land under that subdirectory.
    final assetsRoot = p.join(extractDir.path, 'runtime_assets');
    if (!File(p.join(assetsRoot, 'manifest_spatial.json')).existsSync()) {
      return DeliveryResult(
        ok: false,
        message:
            'Delivered artifact is missing runtime_assets/manifest_spatial.json '
            '— unexpected archive layout. Try again, or report this build.',
      );
    }
    log('  ✓ unpacked spatial runtime');

    // Record what was delivered (audit trail / idempotency).
    File(p.join(cacheDir.path, 'delivered.json')).writeAsStringSync(
      '{"version":"${signed.version}","sha256":"${signed.sha256}",'
      '"deliveredAt":"${DateTime.now().toUtc().toIso8601String()}"}\n',
    );

    return DeliveryResult(
      ok: true,
      message: 'Spatial runtime v${signed.version} delivered.',
      version: signed.version,
      installedTo: dest.path,
      assetsRoot: assetsRoot,
    );
  }
}
