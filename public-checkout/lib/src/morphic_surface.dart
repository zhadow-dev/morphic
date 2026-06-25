import 'dart:async';

import 'package:flutter/services.dart';

import 'app_bus.dart';

/// MORPHIC AUTHORING LAYER (M2.1D) — surface self-identity.
///
/// A surface learns its OWN opaque native id (e.g. `workspace_3`) from the runtime's identity
/// handshake (`surface.identity` over the app bus, pushed once the engine is ready). This is the
/// foundational addressing primitive — it unlocks lifecycle cleanup, targeted orchestration,
/// "open doc X in surface Y", layout restore, telemetry.
///
/// IDENTITY-ONLY by design. There is deliberately NO `currentSurface()` god-object: this exposes
/// an opaque id, never an HWND, engine handle, native pointer, or authority over the surface. The
/// firewall holds upward — the app addresses surfaces by opaque id and composes meaning itself.
class MorphicSurface {
  static String? _id;
  static final List<void Function(String)> _waiters = [];
  static bool _wired = false;

  static void _ensureWired() {
    if (_wired) return;
    _wired = true;
    AppBus.on('surface.identity', (p) {
      final id = p['surfaceId'] as String?;
      if (id == null) return;
      _id = id;
      final waiters = List.of(_waiters);
      _waiters.clear();
      for (final w in waiters) {
        w(id);
      }
    });
  }

  /// This surface's own opaque native id. Completes when the runtime delivers identity (at the
  /// surface's first frame). Safe to await in `initState`.
  static Future<String> currentId() {
    _ensureWired();
    final cached = _id;
    if (cached != null) return Future.value(cached);
    final completer = Completer<String>();
    _waiters.add((id) => completer.complete(id));
    return completer.future;
  }

  /// Synchronous best-effort: this surface's id, or null until identity has arrived.
  static String? get idOrNull => _id;

  // ---------------------------------------------------------------------------------------------
  // M2.3E — SURFACE-LOCAL window controls. These act ONLY on THIS surface (resolved via its own id)
  // — NOT a god-controller over other surfaces. Hybrid authority: native owns minimize/close, the
  // runtime owns SEMANTIC maximize (SetBounds → monitor work area). A surface controls its own window
  // chrome; it still can't reach into anyone else's window or input. (Same channel as spawn/activate.)
  static const MethodChannel _win = MethodChannel('morphic');

  static Future<void> _act(String method) async {
    final id = await currentId();
    await _win.invokeMethod(method, {'surfaceId': id});
  }

  static Future<void> minimize() => _act('surface.minimize');
  static Future<void> maximize() => _act('surface.maximize');
  static Future<void> restore() => _act('surface.restore');
  static Future<void> toggleMaximize() => _act('surface.toggleMaximize');
  static Future<void> close() => _act('surface.close');

  /// Hide THIS surface's window natively (ShowWindow SW_HIDE) without closing it.
  /// Used by the boot shell of a dissolving spatial app so the root chip never
  /// lingers while the scene spawns; the root is closed for real right after.
  static Future<void> hide() => _act('surface.hide');

  // ---------------------------------------------------------------------------------------------
  // SPATIAL CHROME — the window's corner radius as a runtime-owned surface property.
  //
  // The radius is stored in the native policy layer (set at spawn via `SurfaceSpec.cornerRadius`
  // or live via [setCornerRadius]) and DELIVERED to this surface's engine, where
  // [MorphicWindowShape] shapes the window through content alpha over the full-glass frame —
  // antialiased, any radius (0 → a full circle). This is the only shape mechanism the OS allows
  // on this substrate: a window region cannot clip DWM-composited output, and DWM itself offers
  // only fixed preset radii. True shaping requires `backdrop: 'none'` (an ACCENT blur backdrop
  // always fills the rectangle — OS boundary).

  /// This surface's current corner radius in NATIVE pixels (0 = square).
  static Future<int> getCornerRadius() async {
    final id = await currentId();
    final r =
        await _win.invokeMethod<Map>('surface.chrome.get', {'surfaceId': id});
    return (r?['cornerRadius'] as int?) ?? 0;
  }

  /// Set THIS surface's corner radius (native px) at runtime. The runtime stores the new value
  /// and pushes it back to this engine, so a [MorphicWindowShape] wrapper re-shapes live.
  static Future<void> setCornerRadius(int radiusPx) async {
    final id = await currentId();
    await _win.invokeMethod(
        'surface.setCornerRadius', {'surfaceId': id, 'radius': radiusPx});
  }

  /// M2.8.3 — SCENE ZOOM (spatial scenes). Reproject THIS surface's whole composition plane toward
  /// [zoom] (1.0 = authored; larger = closer/immersive, clamped to fill the field). A CAMERA MOVE, not
  /// a window state: the anchor + its companions scale together FROM the authored snapshot, all staying
  /// sovereign HWNDs. The spatial-scene replacement for "maximize".
  static Future<void> zoomScene(double zoom) async {
    final id = await currentId();
    await _win.invokeMethod('scene.zoom', {'surfaceId': id, 'zoom': zoom});
  }

  /// Return this surface's composition to its authored scale (zoom -> 1.0).
  static Future<void> resetSceneZoom() async {
    final id = await currentId();
    await _win.invokeMethod('scene.resetZoom', {'surfaceId': id});
  }

  /// Listen to THIS surface's native window state (maximized / minimized), pushed by the runtime
  /// whenever it changes (a control tap, a titlebar double-click, or an OS taskbar restore). Use it
  /// to flip a maximize/restore icon or adapt chrome. Delivered only to this surface (per-engine push).
  static void onWindowState(void Function(bool maximized, bool minimized) cb) {
    AppBus.on('surface.windowState',
        (p) => cb(p['maximized'] == true, p['minimized'] == true));
  }
}
