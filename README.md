# Morphic

**Build real multi-window Flutter desktop apps. One app, many windows, zero Win32.**

A normal Flutter desktop app is one window with one widget tree. Morphic turns
that same app into a runtime that presents **many sovereign native windows** —
each its own Flutter engine — orchestrated for you (geometry, z-order,
activation, lifecycle). You author it all in Dart.

> ⚠️ Pre-1.0, **Windows-only** today. The install flow and native runtime are
> scratch-verified, but APIs may still evolve.

## Install

```bash
flutter create my_app && cd my_app
flutter pub add morphic
dart run morphic:init        # patches windows/runner to host the runtime (reversible)
flutter run -d windows
```

`morphic:init` hands process bootstrap to the Morphic runtime and materializes
the runtime sources into `windows/runner/`. It leaves `lib/`, your pubspec, and
every non-Windows platform untouched, and is fully reversible
(`dart run morphic:remove`). **No C++, no CMake, no Win32.**

## Your first window

```dart
import 'package:morphic/morphic.dart';

void main() => runMorphicApp(app: MyApp());

class MyApp extends MorphicApp {
  @override
  String get name => 'My App';

  @override
  List<SurfaceSpec> surfaces() => const [
        SurfaceSpec.workspace(id: 'main', entrypoint: 'mainSurface'),
      ];
}
```

## Multiple windows

Declare more surfaces — each becomes its own real window:

```dart
@override
List<SurfaceSpec> surfaces() => const [
      SurfaceSpec.workspace(id: 'editor', entrypoint: 'editor'),
      SurfaceSpec.inspector(id: 'inspector', entrypoint: 'inspector'),
      SurfaceSpec.palette(id: 'tools', entrypoint: 'tools'),
    ];
```

## Talk between windows — `AppBus`

Surfaces are isolated engines, so they share state through a tiny event bus:

```dart
// in the editor window
AppBus.broadcast('doc.changed', {'id': docId});

// in the inspector window
AppBus.on('doc.changed', (p) => reload(p['id'] as String));
```

## Surface lifecycle

The runtime publishes lifecycle events on the same bus, so a surface can react
when another opens or closes:

```dart
AppBus.on('surface.destroyed', (p) => forgetDocFor(p['surfaceId'] as String));
```

A surface can also drive its own window:

```dart
await MorphicSurface.hide();
await MorphicSurface.close();
```

## Native mode

The above is **native mode** — orchestrated real OS windows, the default and
free. Closing the last window exits the app. No account or network required.

## Docs

| Doc | For |
|---|---|
| [Quickstart](doc/QUICKSTART.md) | Install → first surface → run, in ~10 minutes |
| [Concepts](doc/CONCEPTS.md) | What a surface / workspace is, and why Morphic works this way |
| [SDK](doc/SDK.md) | The `package:morphic` API + copy-paste patterns |
| [Integration](doc/INTEGRATION.md) | How `morphic:init` installs the runtime |
| [CLI](doc/CLI.md) | The `dart run morphic:*` commands |

`example/` is a showcase / playground, not where you author your own app — that's
`package:morphic` in your own project.

## Spatial Mode

Morphic also supports an optional **Spatial Runtime** for shaped surfaces,
materials, acrylic effects, workspace composition and advanced desktop
experiences.

Learn more → **https://www.getmorphic.space/spatial**
