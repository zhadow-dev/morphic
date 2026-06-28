# SDK — `package:morphic`

The public Dart API. Everything here comes from one import:

```dart
import 'package:morphic/morphic.dart';
```

The whole surface, at a glance:

| API | What it is |
|---|---|
| [`MorphicApp`](#morphicapp) | declares which surfaces boot |
| [`SurfaceSpec`](#surfacespec) | a declarative descriptor for one surface |
| [`runMorphicApp`](#runmorphicapp) | boots your app |
| [`MorphicSurface`](#morphicsurface) | a surface's view of *itself* |
| [`AppBus`](#appbus) | cross-surface **data** relay |
| [`EcologyController`](#ecologycontroller) | imperative spawn / destroy / activate |

---

## `MorphicApp`

Extend it to declare your app's surfaces.

```dart
class MyApp extends MorphicApp {
  @override
  String get name => 'My App';

  @override
  List<SurfaceSpec> surfaces() => const [
        SurfaceSpec.workspace(id: 'main', entrypoint: 'mainSurface'),
      ];
}
```

Boot it from `main()`:

```dart
void main() => MorphicRuntime.run(app: MyApp());
```

## `SurfaceSpec`

A declarative descriptor for one surface — **not a widget**. Each spec becomes a
separate window + engine running its `entrypoint`.

Named constructors set the behavior (`kind`):

```dart
SurfaceSpec.workspace(id: 'main', entrypoint: 'editor');                 // grounded root window
SurfaceSpec.inspector(id: 'info', entrypoint: 'info', parent: 'main');   // support panel
SurfaceSpec.toolPalette(id: 'tools', entrypoint: 'tools', parent: 'main');
SurfaceSpec.overlay(id: 'cmd', entrypoint: 'palette');                   // transient floater
```

Key fields: `id` (app-local, used to wire `parent`), `entrypoint` (the
`@pragma('vm:entry-point')` function name), `parent`, `x/y/width/height`. Spatial
fields (premium): `backend: 'spatial'`, `shape`, `material`, `materialTint`,
`elevation` — see [RUNTIME_MODES](RUNTIME_MODES.md).

## `runMorphicApp`

Your `main()`:

```dart
void main() => runMorphicApp(app: MyApp());
```

## `MorphicSurface`

A surface's view of **itself** — it acts only on its own window and can never
reach another surface.

```dart
// window controls
MorphicSurface.minimize();
MorphicSurface.maximize();
MorphicSurface.restore();
MorphicSurface.toggleMaximize();
MorphicSurface.close();

// this surface's own opaque id (completes at first frame)
final id = await MorphicSurface.currentId();

// react to OS window-state changes (e.g. flip a maximize icon)
MorphicSurface.onWindowState((maximized, minimized) { /* ... */ });
```

## `AppBus`

The **§4b-safe** way surfaces coordinate: it carries **data only** — never input,
focus, activation, or modality. Each surface is a separate engine, so this is how
they sync facts.

```dart
// surface A
AppBus.broadcast('note.selected', {'id': 'abc'});

// surface B
AppBus.on('note.selected', (payload) {
  final id = payload['id'] as String?;
  // update this surface's view
});
```

> The rule that keeps Morphic predictable: "inspector reflects the active note" is
> a **data** sync (fine). "clicking routes the keyboard to the inspector" is
> input ownership (never — the OS-focused window owns input).

## `EcologyController`

Imperative orchestration when you need to spawn/destroy/activate at runtime
(beyond the declarative boot list).

```dart
final ecology = EcologyController();

// spawn on demand; returns the new surface id (or null if rejected)
final id = await ecology.spawnSurface(
  kind: 'tool_palette', entrypoint: 'tools', parentId: mainId,
);

await ecology.destroySurface(id!);     // ownership cascades (workspace -> its panels)
await ecology.activateSurface(someId); // user-driven "go there" (never routes keyboard)
```

---

## Patterns

**Spawn a surface on a button tap**
```dart
onPressed: () => EcologyController().spawnSurface(
  kind: 'overlay', entrypoint: 'palette',
),
```

**Coordinate data between surfaces**
```dart
AppBus.broadcast('doc.changed', {'id': docId});            // sender
AppBus.on('doc.changed', (p) => reload(p['id'] as String)); // receiver
```

**Clean up when this surface dies** — the runtime publishes generic lifecycle
facts on the bus; the app composes meaning from opaque ids:
```dart
AppBus.on('surface.destroyed', (p) => forgetDocFor(p['surfaceId']));
```

## Stability contract

This is the public surface. Names here are intended to stay stable. Anything
**not** in `package:morphic/morphic.dart` (e.g. runtime topology terms like
`shell_root`, retention internals) is internal and not part of the contract —
see [ARCHITECTURE](../ARCHITECTURE.md) / [internals](internals/).
