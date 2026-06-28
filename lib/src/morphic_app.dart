import 'surface_spec.dart';

/// MORPHIC AUTHORING LAYER (M2.2B) — MorphicApp.
///
/// A Morphic app DECLARES its surfaces as [SurfaceSpec] descriptors. That's the whole contract for
/// boot today: "here are my surfaces and how they relate." The runtime realizes them as separate
/// engines/HWNDs; the app never touches C++, channels, or HWNDs.
///
/// Deliberately minimal. App-level *state* coordination (active document, layout, restore) is
/// `AppSession` — NOT built yet, because with one document it would be ahead of its pain. It is
/// earned when multiple documents exist (see M2_FRICTION_LOG). Declarative *relationships* beyond
/// parent/composed are `SurfaceCoordinator` — likewise deferred until several relationships exist.
abstract class MorphicApp {
  /// Human-facing app name (shown by the boot shell; future: window/session identity).
  String get name;

  /// The surfaces this app brings up at boot, in spawn order. A spec's `parent` references another
  /// spec's app-local `id`; the boot resolves it to the real native id.
  List<SurfaceSpec> surfaces();
}
