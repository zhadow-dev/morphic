import 'package:flutter/material.dart';

import 'ecology_controller.dart';
import 'morphic_app.dart';
import 'morphic_surface.dart';
import 'surface_spec.dart';

/// MORPHIC AUTHORING LAYER (M2.2B) — boot + spawn.
///
/// `runMorphicApp(app: NotesApp())` is the honest replacement for hand-written launcher hacks +
/// raw spawn buttons + manual parent plumbing. The boot engine renders a minimal app shell and
/// orchestrates spawning the app's surfaces — it does NOT render them inline (they are separate
/// engines). This keeps the multi-engine truth visible while the *authoring* feels Flutter-native.

final EcologyController _ecology = EcologyController();

/// DX — SESSION RECONCILIATION (P1). A Flutter **hot restart** re-runs `main()`
/// in the root isolate, but the native runtime and the surfaces it has already
/// spawned are C++-owned and survive the restart — so a naive re-spawn stacks
/// duplicate windows (1 → 2 → 3 …).
///
/// The fix is generation-aware boot: snapshot the surfaces that exist *before*
/// this boot spawns (orphans from the previous generation), spawn this boot's
/// surfaces, then reap ONLY those orphans. Reaping AFTER spawning keeps the live
/// surface count > 0 throughout, so it can never trip the runtime's "last surface
/// closed → exit". Cold start: the snapshot is empty, so this is a no-op (and in
/// production `main()` runs once, so there are never orphans).
///
/// Set to `false` for instant rollback to the legacy (duplicating) boot.
const bool kSessionReconcile = true;

/// The fixed native id of the bootstrap launcher surface (it runs the app's
/// `main()`). It persists across a hot restart, so the P1 reconciler must NEVER
/// reap it — doing so would destroy the very engine running this code. Matches
/// the id the native runtime spawns the launcher with.
const String _kLauncherSurfaceId = 'launcher';

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

/// Boot a Morphic app: this becomes the app's `main()`.
///
///     void main() => runMorphicApp(app: NotesApp());
void runMorphicApp({required MorphicApp app}) {
  WidgetsFlutterBinding.ensureInitialized();
  runApp(_MorphicBootShell(app: app));
}

class _MorphicBootShell extends StatefulWidget {
  final MorphicApp app;
  const _MorphicBootShell({required this.app});

  @override
  State<_MorphicBootShell> createState() => _MorphicBootShellState();
}

class _MorphicBootShellState extends State<_MorphicBootShell> {
  int _spawnCount = 0;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) async {
      // A dissolving root hides itself natively BEFORE spawning the scene, so
      // the boot chip never lingers on screen while the surfaces come up.
      if (widget.app.dissolveRootIntoScene) {
        await MorphicSurface.hide();
      }
      // P1 — snapshot orphan surfaces from a previous boot generation BEFORE we
      // spawn this boot's surfaces. Best-effort: reconciliation must never block
      // boot, so any channel error just falls back to the legacy behavior.
      List<String> orphans = const [];
      if (kSessionReconcile) {
        try {
          orphans = await _ecology.currentSurfaceIds();
        } catch (_) {
          orphans = const [];
        }
      }
      await spawnApp(widget.app);
      // Reap ONLY the orphans, and only AFTER this boot's surfaces exist — so the
      // live count stays > 0 and the runtime never sees "all surfaces closed".
      // NEVER reap the bootstrap launcher: it is the surface running THIS main().
      for (final id in orphans) {
        if (id == _kLauncherSurfaceId) continue;
        try {
          await _ecology.destroySurface(id);
        } catch (_) {
          // best-effort; a failed reap just leaves one stale window, never a crash
        }
      }
      // M2.8 — a spatial-native scene IS its own app-presence; the boot root has no spatial meaning,
      // so it DISSOLVES once it has bootstrapped the surfaces. Closing (not hiding) keeps lifetime
      // honest: the scene's surfaces hold the count > 0 now, and closing them all later quits cleanly.
      if (widget.app.dissolveRootIntoScene) {
        await MorphicSurface.close();
      }
    });
  }

  // M2.3F — the chip's reason to exist: spawn the app's PRIMARY surface (its first spec) on tap.
  // App-agnostic — it knows the spec, not what a "note" is. This is the earned empty-state/home
  // affordance (when every document is closed, the chip is how you make a new one). NOT a launcher.
  void _spawnPrimary() {
    final specs = widget.app.surfaces();
    if (specs.isEmpty) return;
    final s = specs.first;
    final off = 28 * (_spawnCount++ % 8); // cascade so repeats don't stack
    _ecology.spawnSurface(
      kind: s.kind,
      entrypoint: s.entrypoint,
      x: s.x + off,
      y: s.y + off,
      width: s.width,
      height: s.height,
    );
  }

  @override
  Widget build(BuildContext context) {
    // M2.8 — spatial-native scene: render NOTHING (transparent + empty) while the root dissolves, so
    // no chip ever competes with the real anchor. The scene's own surfaces are the entire experience.
    if (widget.app.dissolveRootIntoScene) {
      return const MaterialApp(
        debugShowCheckedModeBanner: false,
        home: Scaffold(
            backgroundColor: Colors.transparent, body: SizedBox.shrink()),
      );
    }
    return MaterialApp(
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark(useMaterial3: true).copyWith(
        scaffoldBackgroundColor: const Color(0xFF0D1117),
      ),
      home: Scaffold(
        backgroundColor: const Color(0xFF11151B),
        // M2.3E — app-presence as a small, INTENTIONAL chip, not a slab. The root engine is
        // structurally persistent (can't be a document, can't be closed) and has no operational
        // identity — so it reads as a compact, soft "app is here" pill (small window = ambient
        // presence, not a broken/forgotten panel). Truly hiding it needs the host-lifetime redesign
        // (HiddenRootEngine), still deferred; this is the visual/behavioral fix. App-agnostic: it
        // knows only the app's name, never what a surface IS.
        body: Center(
          child: Tooltip(
            message: 'New ${widget.app.name} surface',
            child: MouseRegion(
              cursor: SystemMouseCursors.click,
              child: GestureDetector(
                onTap: _spawnPrimary,
                child: Row(
                  mainAxisSize: MainAxisSize.min,
                  children: [
                    Container(
                      width: 9,
                      height: 9,
                      decoration: const BoxDecoration(
                        shape: BoxShape.circle,
                        gradient: LinearGradient(
                            colors: [Color(0xFF58A6FF), Color(0xFFD2A8FF)]),
                      ),
                    ),
                    const SizedBox(width: 10),
                    Text(widget.app.name,
                        style: const TextStyle(
                            color: Color(0xFFAEB6C0),
                            fontSize: 13,
                            fontWeight: FontWeight.w600,
                            letterSpacing: 0.3)),
                    const SizedBox(width: 10),
                    const Icon(Icons.add, size: 14, color: Color(0xFF49525C)),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}
