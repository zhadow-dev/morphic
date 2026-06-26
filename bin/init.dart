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
    if (args.contains('--spatial')) {
      final ok = await _deliverSpatial();
      if (!ok) exit(MorphicExit.refused);
    }
    exit(await buildMorphicRunner().run(['init', ...args]) ?? MorphicExit.ok);
  } on UsageException catch (e) {
    stderr.writeln(e);
    exit(MorphicExit.usage);
  } catch (e) {
    stderr.writeln('morphic: $e');
    exit(MorphicExit.refused);
  }
}

/// Runs the authenticated, signature-verified spatial delivery pipeline.
/// Returns false (with a printed reason) if the user must act first.
Future<bool> _deliverSpatial() async {
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
      return false;
    }
    stdout.writeln('\n✓ ${result.message}\n');
    return true;
  } on MorphicApiException catch (e) {
    stderr.writeln('\nSpatial delivery failed: $e');
    return false;
  } finally {
    api.close();
  }
}
