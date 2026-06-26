import 'dart:io';

import 'package:args/command_runner.dart';
import 'package:morphic/src/integrator/integrator.dart';
import 'package:morphic/src/licensing/api_client.dart';
import 'package:morphic/src/licensing/auth_store.dart';
import 'package:morphic/src/licensing/spatial_delivery.dart';

/// `dart run morphic:init [...]` — colon-form alias for `dart run morphic init`.
/// Transforms this Flutter project's Windows runner into a Morphic-hosted
/// runtime (idempotent, reversible). Add `--apply` to perform it.
///
/// With `--spatial`, the premium GPU runtime is first delivered securely
/// (login → authorize → signed URL → download → verify) before init proceeds.
Future<void> main(List<String> args) async {
  try {
    // For the spatial tier, deliver + unpack the license-gated runtime first,
    // then point init at the EXTRACTED artifact (its compositor_ng/ sources are
    // never in the published package). Native init uses the bundled assets.
    String? spatialAssetsRoot;
    if (args.contains('--spatial')) {
      spatialAssetsRoot = await _deliverSpatial();
      if (spatialAssetsRoot == null) exit(MorphicExit.refused);
    }
    final env =
        spatialAssetsRoot != null
            ? CliEnvironment(assetsRoot: spatialAssetsRoot)
            : CliEnvironment();
    exit(
      await buildMorphicRunner(environment: env).run(['init', ...args]) ??
          MorphicExit.ok,
    );
  } on UsageException catch (e) {
    stderr.writeln(e);
    exit(MorphicExit.usage);
  } catch (e) {
    stderr.writeln('morphic: $e');
    exit(MorphicExit.refused);
  }
}

/// Runs the authenticated, signature-verified spatial delivery pipeline.
/// Returns the extracted `runtime_assets/` root to materialize from, or null
/// (with a printed reason) if the user must act first / delivery failed.
Future<String?> _deliverSpatial() async {
  stdout.writeln('Step 1 of 2 — Spatial runtime delivery\n');
  final api = MorphicApi();
  try {
    final result = await SpatialDelivery(
      api: api,
      store: AuthStore(),
      log: stdout.writeln,
    ).ensure(projectRoot: Directory.current.path);

    if (!result.ok) {
      stderr.writeln('\n${result.message}');
      return null;
    }
    stdout.writeln('\n✓ ${result.message}\n');
    return result.assetsRoot;
  } on MorphicApiException catch (e) {
    stderr.writeln('\nSpatial delivery failed: $e');
    return null;
  } finally {
    api.close();
  }
}
