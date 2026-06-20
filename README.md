# Morphic

**Build real multi-window Flutter desktop apps. One app, many windows, zero Win32.**

A normal Flutter desktop app is one window with one widget tree. Morphic turns
that same app into a runtime that presents **many sovereign native windows** —
each its own Flutter engine — orchestrated for you (geometry, z-order,
activation, lifecycle). You author it all in Dart.

> ⚠️ Pre-1.0, **Windows-only** today. The install flow and native runtime are
> scratch-verified, but APIs may still evolve.

## Prerequisites

- **Flutter** (desktop enabled): `flutter config --enable-windows-desktop`
- **Visual Studio** (not VS Code) with the **“Desktop development with C++”**
  workload — Flutter needs it to build Windows apps. Verify with `flutter doctor`.

## Install

```bash
flutter create my_app && cd my_app
flutter pub add morphic
dart run morphic:init --apply   # patches windows/runner to host the runtime (reversible)
flutter pub get
flutter run -d windows
```

> `morphic:init` is a **dry run** without `--apply` — it prints the plan but
> changes nothing. Use `--apply` to actually install. It leaves `lib/`, your
> pubspec, and every non-Windows platform untouched, and is fully reversible
> (`dart run morphic:remove --apply`). **No C++, no CMake, no Win32.**

## Your first window

A **surface** is a window. Its content is an ordinary Flutter app behind a
top-level `@pragma('vm:entry-point')` function the runtime launches by name:

```dart
// lib/main.dart
import 'package:flutter/material.dart';
import 'package:morphic/morphic.dart';

@pragma('vm:entry-point')
void mainSurface() => runApp(
      const MaterialApp(home: Scaffold(body: Center(child: Text('Hello, Morphic')))),
    );

class MyApp extends MorphicApp {
  @override
  String get name => 'My App';

  @override
  List<SurfaceSpec> surfaces() => const [
        SurfaceSpec.workspace(id: 'main', entrypoint: 'mainSurface'),
      ];
}

void main() => runMorphicApp(app: MyApp());
```

## Multiple windows

Add another spec **and its entrypoint** — each surface is its own engine:

```dart
@pragma('vm:entry-point')
void inspector() => runApp(
      const MaterialApp(home: Scaffold(body: Center(child: Text('Inspector')))),
    );

// in surfaces():
List<SurfaceSpec> surfaces() => const [
      SurfaceSpec.workspace(id: 'main', entrypoint: 'mainSurface'),
      SurfaceSpec.inspector(id: 'info', entrypoint: 'inspector', parent: 'main'),
    ];
```

(`SurfaceSpec` also has `.toolPalette(...)` and `.overlay(...)`.)

## Talk between windows — `AppBus`

Surfaces are isolated engines, so they share state through a tiny event bus:

```dart
// in the editor window
AppBus.broadcast('doc.changed', {'id': docId});

// in the inspector window
AppBus.on('doc.changed', (p) => reload(p['id'] as String));
```

## Surface lifecycle

The runtime publishes lifecycle events on the same bus, and a surface can drive
its own window (it only ever acts on itself):

```dart
AppBus.on('surface.destroyed', (p) => forgetDocFor(p['surfaceId'] as String));

MorphicSurface.minimize();
MorphicSurface.toggleMaximize();
MorphicSurface.close();
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
