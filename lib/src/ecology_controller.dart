import 'package:flutter/services.dart';

/// PHASE 10.2 / 11–12 — EcologyController.
///
/// Example-app-only Dart API that routes surface spawn/destroy commands through the
/// native SurfaceEcology layer (the product policy facade). This is NOT part of the
/// Morphic library — it lives in the example app because ecology semantics are
/// experimental and must NOT leak into the runtime.
///
/// All spawns go through:
///   EcologyController → method channel ('morphic') → MorphicPlugin extension handler
///     → MorphicRuntime → SurfaceEcology → SurfacePolicy (gate) → SurfaceRegistry →
///     WorkspaceManager → SurfaceManager::CreateSurface (runtime entry, unchanged)
/// The runtime never learns SurfaceKind. Product semantics stay above.
///
/// PHASE 11/12 — per-surface APPEARANCE + COMPOSITION can now be set at spawn:
///   corners      : 'default' | 'rounded' | 'small' | 'square' (DWM presets, antialiased)
///   cornerRadius : ANY radius in px via a window region (edges are aliased — prefer
///                  the DWM presets unless you need a specific radius)
///   chromeless   : true → no native title strip / resize borders; content fills the
///                  window (position the surface programmatically — no grab zones)
///   shadow   : true → native DWM drop shadow; false → none
///   backdrop : 'none' | 'mica' | 'acrylic' | 'tabbed'
///              CAVEAT: only VISIBLE if the surface's Flutter content paints a
///              TRANSPARENT background — the opaque default hides it.
///   composed : true → marks the surface as a Composed (shared-drag) member.
///              CAVEAT: records the ATTRIBUTE only. Actual shared-drag MOVEMENT
///              (a workspace drag carrying the surface) is Phase 12D and is NOT wired
///              yet — composed=true does not move surfaces together today.
/// Any field left null falls back to the kind's policy default.
class EcologyController {
  static const _channel = MethodChannel('morphic');

  /// Build the optional appearance/composition args, omitting nulls so the native
  /// layer falls back to per-kind policy defaults.
  Map<String, Object> _features(
      {String? corners,
      bool? shadow,
      String? backdrop,
      bool? composed,
      String? transparency,
      bool? chromeless,
      int? cornerRadius,
      String? backend,
      String? shape,
      String? material,
      int? materialTint,
      int? elevation}) {
    final m = <String, Object>{};
    if (corners != null) m['corners'] = corners;
    if (shadow != null) m['shadow'] = shadow;
    if (backdrop != null) m['backdrop'] = backdrop;
    if (composed != null) m['composed'] = composed;
    // PHASE 11E — 'opaque' | 'titlebar' | 'transparent_content' | 'full_glass' | 'hybrid'.
    // Proven today: opaque + transparent_content/full_glass. titlebar/hybrid route but
    // project as opaque until the material-aware shell paint lands (11E-B).
    if (transparency != null) m['transparency'] = transparency;
    // SPATIAL CHROME — chromeless removes the native title strip + resize borders
    // (full-bleed content; the surface is positioned programmatically, not by grab).
    // cornerRadius (px) clips the window to ANY radius via a window region —
    // edges are aliased; the DWM `corners` presets are the antialiased path.
    if (chromeless != null) m['chromeless'] = chromeless;
    if (cornerRadius != null) m['cornerRadius'] = cornerRadius;
    // MORPHIC NG -- 'native' (default) | 'spatial' (composited shaped GPU visual).
    if (backend != null) m['backend'] = backend;
    // MORPHIC NG Phase 3 -- spatial material spec: compositor-level shape
    // (rounded/capsule/hexagon), REAL backdrop material, tint, shadow depth.
    if (shape != null) m['shape'] = shape;
    if (material != null) m['material'] = material;
    if (materialTint != null) m['materialTint'] = materialTint;
    if (elevation != null) m['elevation'] = elevation;
    return m;
  }

  /// Generic spawn — any kind ('workspace' | 'tool_palette' | 'inspector' | 'overlay')
  /// with full per-surface appearance/composition control. Returns the surface id or
  /// null if policy rejected the spawn.
  Future<String?> spawnSurface({
    required String kind,
    int x = 200,
    int y = 200,
    int width = 400,
    int height = 300,
    String? parentId,
    // M2.2 friction #1 — run an app's OWN Dart content in a surface of [kind] by naming its
    // @pragma('vm:entry-point') function (exported from main.dart). Null = the kind's default
    // entrypoint (the ecology demo's surfaces). This is what lets a SECOND APP be Dart-only.
    String? entrypoint,
    String? corners,
    bool? shadow,
    String? backdrop,
    bool? composed,
    String? transparency,
    bool? chromeless,
    int? cornerRadius,
    String? backend,
    String? shape,
    String? material,
    int? materialTint,
    int? elevation,
  }) async {
    final result = await _channel.invokeMethod<Map>('ecology.spawn', {
      'kind': kind,
      'x': x,
      'y': y,
      'width': width,
      'height': height,
      if (parentId != null) 'parentId': parentId,
      if (entrypoint != null) 'entrypoint': entrypoint,
      ..._features(
          corners: corners,
          shadow: shadow,
          backdrop: backdrop,
          composed: composed,
          transparency: transparency,
          chromeless: chromeless,
          cornerRadius: cornerRadius,
          backend: backend,
          shape: shape,
          material: material,
          materialTint: materialTint,
          elevation: elevation),
    });
    return result?['id'] as String?;
  }

  /// Spawn a workspace surface. Returns the surface id or null if rejected.
  Future<String?> spawnWorkspace({
    int? x,
    int? y,
    int? width,
    int? height,
    String? corners,
    bool? shadow,
    String? backdrop,
    bool? composed,
  }) async {
    final result = await _channel.invokeMethod<Map>('ecology.spawn', {
      'kind': 'workspace',
      'x': x ?? 120,
      'y': y ?? 120,
      'width': width ?? 600,
      'height': height ?? 480,
      ..._features(
          corners: corners, shadow: shadow, backdrop: backdrop, composed: composed),
    });
    return result?['id'] as String?;
  }

  /// Spawn a tool palette owned by [parentId]. Returns the surface id or null if
  /// policy rejected the spawn (e.g. parent is not a workspace).
  Future<String?> spawnToolPalette({
    required String parentId,
    int? x,
    int? y,
    String? corners,
    bool? shadow,
    String? backdrop,
    bool? composed,
  }) async {
    final result = await _channel.invokeMethod<Map>('ecology.spawn', {
      'kind': 'tool_palette',
      'parentId': parentId,
      'x': x ?? 800,
      'y': y ?? 120,
      'width': 240,
      'height': 320,
      ..._features(
          corners: corners, shadow: shadow, backdrop: backdrop, composed: composed),
    });
    return result?['id'] as String?;
  }

  /// Spawn an inspector owned by [parentId]. Returns the surface id or null.
  Future<String?> spawnInspector({
    required String parentId,
    int? x,
    int? y,
    String? corners,
    bool? shadow,
    String? backdrop,
    bool? composed,
  }) async {
    final result = await _channel.invokeMethod<Map>('ecology.spawn', {
      'kind': 'inspector',
      'parentId': parentId,
      'x': x ?? 800,
      'y': y ?? 480,
      'width': 280,
      'height': 400,
      ..._features(
          corners: corners, shadow: shadow, backdrop: backdrop, composed: composed),
    });
    return result?['id'] as String?;
  }

  /// Spawn a transient overlay surface. Returns the surface id or null.
  Future<String?> spawnOverlay({
    int? x,
    int? y,
    String? corners,
    bool? shadow,
    String? backdrop,
    bool? composed,
  }) async {
    final result = await _channel.invokeMethod<Map>('ecology.spawn', {
      'kind': 'overlay',
      'x': x ?? 300,
      'y': y ?? 200,
      'width': 360,
      'height': 300,
      ..._features(
          corners: corners, shadow: shadow, backdrop: backdrop, composed: composed),
    });
    return result?['id'] as String?;
  }

  /// M2.2E — USER-DRIVEN activation: bring [surfaceId] to the OS foreground (the runtime owns
  /// activation; this only relays an explicit user "go there", e.g. a command-palette switch).
  /// §4b-safe: never routes keyboard — the OS foreground does.
  Future<void> activateSurface(String surfaceId) async {
    await _channel.invokeMethod('surface.activate', {'surfaceId': surfaceId});
  }

  /// M2.2E.1 — TRANSIENT activation handoff: foreground [target], THEN destroy [closeId], as ONE
  /// ordered runtime op. Because the closing transient (a command palette) is already background
  /// when it dies, Windows restores nothing — killing the "focus jumps to the previously-focused
  /// surface" race that two separate activate+destroy calls cause. §4b-safe (user-driven; runtime
  /// owns ordering, never the keyboard). Narrow to the palette workflow — generalize only if a
  /// second transient earns it.
  Future<void> handoffActivation({required String target, required String closeId}) async {
    await _channel.invokeMethod('surface.handoff', {'activate': target, 'close': closeId});
  }

  /// Destroy a surface by ecology id. Triggers ownership cascade (e.g. destroying a
  /// workspace also destroys its palettes/inspectors).
  Future<bool> destroySurface(String id) async {
    final result = await _channel.invokeMethod<bool>('ecology.destroy', {'id': id});
    return result ?? false;
  }

  /// Get the current ecology summary — surfaces, workspaces, ownership.
  Future<Map<String, dynamic>> getSummary() async {
    final result = await _channel.invokeMethod<Map>('ecology.summary');
    return result?.cast<String, dynamic>() ?? {};
  }

  /// The native ids of every surface currently live in the ecology. Used by the
  /// boot reconciler ([runMorphicApp]) to find surfaces left over from a previous
  /// boot generation — a Flutter hot restart re-runs `main()` in the root isolate,
  /// but the native runtime and its already-spawned surfaces persist.
  Future<List<String>> currentSurfaceIds() async {
    final summary = await getSummary();
    final surfaces = (summary['surfaces'] as List?) ?? const [];
    return [
      for (final s in surfaces)
        if (s is Map && s['id'] is String) s['id'] as String,
    ];
  }
}
