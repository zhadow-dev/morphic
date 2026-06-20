# Quickstart

A working Morphic app — a real, sovereign desktop window authored in Dart — in
about 10 minutes. No spatial concepts, no Win32, no C++.

## 0. Prerequisites

- **Flutter** with Windows desktop enabled (`flutter config --enable-windows-desktop`).
- **Visual Studio** (not VS Code) with the **“Desktop development with C++”**
  workload — required to build any Flutter Windows app. Run `flutter doctor` and
  make sure the Windows check is green before continuing.

## 1. Create a project and add Morphic

```bash
flutter create my_app
cd my_app
flutter pub add morphic
dart run morphic:init --apply
flutter pub get
```

`dart run morphic:init --apply` transforms `windows/runner` to host the Morphic
runtime (reversible; see [INTEGRATION](INTEGRATION.md)). It never touches `lib/`.

## 2. Write a surface

A **surface** is a window. Its content is an ordinary Flutter app behind a
top-level `@pragma('vm:entry-point')` function (the runtime launches it by name):

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

That's the whole contract: **entrypoints** are what runs inside surfaces; a
**`MorphicApp`** declares which surfaces boot. `@pragma('vm:entry-point')` keeps
the function from being tree-shaken so the runtime can launch it by name.

## 3. Run

```bash
flutter run -d windows
```

A real desktop window appears — draggable, resizable, with working minimize /
maximize / close.

## 4. Add a second window

Just add another spec (and its entrypoint). Each surface is its own engine:

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

## 5. A surface controlling its own window

A surface acts only on **itself** (it can never reach another window):

```dart
import 'package:morphic/morphic.dart';

MorphicSurface.minimize();
MorphicSurface.toggleMaximize();
MorphicSurface.close();
```

## The one rule

Each surface is a separate engine, so **the focused window owns the keyboard** —
you never route input between surfaces. If two surfaces must share *data*, use
[`AppBus`](SDK.md#appbus) (data only, never input/focus). This single rule is what
keeps Morphic apps predictable.

## Next

- **Why** it works this way → [CONCEPTS](CONCEPTS.md)
- The full API + patterns → [SDK](SDK.md)
- Native vs spatial → [RUNTIME_MODES](RUNTIME_MODES.md)

## Undo

```bash
dart run morphic:remove --apply   # restores your project byte-for-byte
```
