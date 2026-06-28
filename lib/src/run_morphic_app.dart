import 'package:flutter/widgets.dart';

import 'ecology_controller.dart';
import 'morphic_app.dart';
import 'surface_spec.dart';

/// MORPHIC RUNTIME CORE (ABI 0.2) — application bootstrap.
///
/// THE INVARIANT: the Runtime never displays UI. `main()` runs on a windowless
/// bootstrap engine that the native runtime owns; it orchestrates the app's
/// surfaces and renders nothing. There is no launcher window and no widget tree
/// here — every visible window is an application Surface.

final EcologyController _ecology = EcologyController();

/// DX — SESSION RECONCILIATION (P1). A Flutter **hot restart** re-runs `main()`
/// on the bootstrap engine, but the native runtime and the surfaces it has
/// already spawned are C++-owned and survive the restart — so a naive re-spawn
/// stacks duplicate windows (1 → 2 → 3 …).
///
/// The fix is generation-aware boot: snapshot the surfaces that exist *before*
/// this boot spawns (orphans from the previous generation), spawn this boot's
/// surfaces, then reap ONLY those orphans. Reaping AFTER spawning keeps the live
/// surface count > 0 throughout, so it never trips the runtime's "last surface
/// closed → exit". Cold start: the snapshot is empty (and in production `main()`
/// runs once, so there are never orphans). Note: the bootstrap engine is NOT a
/// tracked surface, so it can never appear in the orphan set.
const bool kSessionReconcile = true;

/// Spawn an app's [SurfaceSpec]s, resolving in-spec `parent` references (app-local ids) to the
/// real native ids. Returns the spec-id → native-id map. Pure orchestration over the raw channel;
/// no widget composition, no fiction.
Future<Map<String, String>> spawnApp(MorphicApp app) async {
  final idMap = <String, String>{};
  for (final s in app.surfaces()) {
    final parentNative = s.parent != null ? idMap[s.parent!] : null;
    final nativeId = await _ecology.spawnSurface(
      kind: s.kind,
      entrypoint: s.entrypoint,
      parentId: parentNative,
      composed: s.composed,
      x: s.x,
      y: s.y,
      width: s.width,
      height: s.height,
      backdrop: s.backdrop,
      transparency: s.transparency,
      corners: s.corners,
      cornerRadius: s.cornerRadius,
      chromeless: s.chromeless ? true : null,
      backend: s.backend,
      shape: s.shape,
      material: s.material,
      materialTint: s.materialTint,
      elevation: s.elevation,
    );
    if (nativeId != null) idMap[s.id] = nativeId;
  }
  return idMap;
}

/// The Morphic application runtime. The runtime — never a window — owns the
/// application's lifetime; this is the developer's entry point.
///
///     void main() => MorphicRuntime.run(app: MyApp());
abstract final class MorphicRuntime {
  /// Boot a Morphic application. Initializes the binding for platform channels
  /// (no `runApp`: the bootstrap engine is headless and renders nothing), then
  /// reconciles + spawns the app's surfaces.
  static Future<void> run({required MorphicApp app}) async {
    WidgetsFlutterBinding.ensureInitialized();

    // P1 — snapshot orphans from a previous boot generation BEFORE spawning.
    // Best-effort: reconciliation must never block boot.
    List<String> orphans = const [];
    if (kSessionReconcile) {
      try {
        orphans = await _ecology.currentSurfaceIds();
      } catch (_) {
        orphans = const [];
      }
    }
    await spawnApp(app);
    // Reap ONLY the orphans, AFTER this boot's surfaces exist — so the live
    // count stays > 0 and the runtime never sees "all surfaces closed".
    for (final id in orphans) {
      try {
        await _ecology.destroySurface(id);
      } catch (_) {
        // best-effort; a failed reap leaves one stale window, never a crash
      }
    }
  }
}

/// DEPRECATED — use [MorphicRuntime.run]. The runtime now owns the application;
/// this is a forwarding shim removed after ABI 0.2.
@Deprecated('Use MorphicRuntime.run(app: ...) instead.')
void runMorphicApp({required MorphicApp app}) => MorphicRuntime.run(app: app);
