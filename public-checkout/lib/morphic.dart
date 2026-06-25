/// Morphic — a spatial multi-surface desktop runtime for Flutter (Windows).
///
/// This is the **Dart authoring SDK**: the API you write your Morphic app
/// against. Import it and stay in Dart —
///
/// ```dart
/// import 'package:morphic/morphic.dart';
///
/// void main() => runMorphicApp(app: MyApp());
///
/// class MyApp extends MorphicApp {
///   @override
///   String get name => 'My App';
///   @override
///   List<SurfaceSpec> surfaces() => [
///         SurfaceSpec.workspace(id: 'main', entrypoint: 'myMain'),
///       ];
/// }
/// ```
///
/// The native runtime that hosts these surfaces (one Flutter engine + window
/// each) is installed into your project's `windows/runner/` by the integrator
/// (`dart run morphic_cli:morphic init`). You never touch C++, CMake, or HWNDs —
/// see `doc/GUIDE.md`.
///
/// What this SDK gives you:
///   - [MorphicApp] / [SurfaceSpec] — declare which surfaces boot and how they
///     relate (orchestration descriptors, not a widget tree).
///   - [runMorphicApp] — boot the app.
///   - [MorphicSurface] — a surface controls its OWN window (minimize/maximize/
///     close/identity/scene-zoom); it can never reach another surface.
///   - [AppBus] — §4b-safe cross-surface DATA relay (never input/focus/topology).
///   - [EcologyController] — imperative spawn/destroy/activate orchestration.
///
/// (The pre-pivot Dart API — `MorphicController` / `createSurface` /
/// `attachRenderer` / "Studio" — was the abandoned compositor-as-plugin lineage
/// and is quarantined under `legacy/`. Do not resurrect it.)
library;

export 'src/app_bus.dart';
export 'src/ecology_controller.dart';
export 'src/morphic_app.dart';
export 'src/morphic_surface.dart';
export 'src/run_morphic_app.dart';
export 'src/surface_spec.dart';
