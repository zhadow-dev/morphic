import 'package:flutter/services.dart';

/// MORPHIC AUTHORING LAYER — AppBus: the cross-surface coordination spine (platform-level).
///
/// Each Morphic surface is a SEPARATE Flutter engine, so surfaces cannot share Dart state. AppBus
/// is the **§4b-SAFE** way they coordinate: a surface `broadcast`s `{topic, payload}`; the runtime
/// forwards it (a DUMB transport — `app.broadcast` on the `morphic` channel) to every surface's
/// `morphic/app` channel; subscribers filter by topic.
///
/// It also receives the runtime's GENERIC lifecycle/identity facts (pushed by
/// SurfaceLifecycleRelay): `surface.created` / `surface.destroyed` / `surface.identity`, each
/// carrying only an opaque `surfaceId`. The app composes meaning upward from these.
///
/// CRITICAL (§4b): this carries **DATA / facts only** — documents, metadata, surface ids. It must
/// **never** carry input, keyboard focus, activation, capture, or modality (those stay HWND-native).
/// "Inspector reflects the active note" / "remove the doc whose surface died" are data syncs;
/// "clicking routes the keyboard to the inspector" would be an input-ownership violation.
///
/// The runtime never learns what a "note" is — it only relays opaque facts.
class AppBus {
  // Outbound: invoke goes to the C++ extension handler (same channel as EcologyController).
  static const MethodChannel _out = MethodChannel('morphic');
  // Inbound: the runtime relay pushes `app.event` here, per surface engine.
  static const MethodChannel _in = MethodChannel('morphic/app');

  static final Map<String, List<void Function(Map)>> _subs = {};
  static bool _wired = false;

  static void _ensureWired() {
    if (_wired) return;
    _wired = true;
    _in.setMethodCallHandler((call) async {
      if (call.method == 'app.event' && call.arguments is Map) {
        final args = call.arguments as Map;
        final topic = args['topic'] as String?;
        if (topic != null) {
          final payload = args['payload'] is Map ? args['payload'] as Map : const {};
          for (final cb in List.of(_subs[topic] ?? const [])) {
            cb(payload);
          }
        }
      }
      return null;
    });
  }

  /// Broadcast an app event to ALL surfaces (including self — subscribers filter by topic).
  static void broadcast(String topic, Map<String, Object?> payload) {
    _ensureWired();
    _out.invokeMethod('app.broadcast', {'topic': topic, 'payload': payload});
  }

  /// Subscribe a callback to a topic. (No unsubscribe yet — app surfaces are long-lived; add if
  /// lifecycle churn proves it necessary — that itself is a friction-log entry.)
  static void on(String topic, void Function(Map payload) cb) {
    _ensureWired();
    (_subs[topic] ??= []).add(cb);
  }
}
