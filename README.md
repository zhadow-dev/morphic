# Morphic

**A Flutter package that adds a native multi-surface runtime to your Flutter desktop app (Windows).**

A normal Flutter desktop app is one window with one widget tree. Morphic lets one
app present **many sovereign windows** — each its own Flutter engine — that the
runtime orchestrates (geometry, z-order, activation, lifecycle) while staying
agnostic to what any window contains. You author it all in Dart.

⚠️ Morphic is pre-1.0, Windows-only, and currently focused on desktop runtime stabilization.

The install flow and native runtime are operational and scratch-verified, but APIs and runtime behavior may still evolve rapidly.

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

## Two runtime modes

Morphic has **two genuinely different runtime shapes** — not "the same thing with
extra effects":

- **Native mode** — orchestrated multi-window runtime. Sovereign OS windows;
  closing the last one exits the app. The default, free.
- **Spatial mode** — a composited spatial runtime: GPU-composited, shaped,
  materialized surfaces inside a persistent shell. *Premium.*

See **[doc/RUNTIME_MODES.md](doc/RUNTIME_MODES.md)**.

## Install & first run

```bash
flutter create my_app && cd my_app
flutter pub add morphic
dart run morphic:init        # transforms windows/runner to host the runtime (reversible)
# point main() at runMorphicApp(...), then:
flutter run -d windows
```

`dart run morphic:init` patches your Windows runner to hand process bootstrap to
the Morphic runtime and materializes the runtime sources into `windows/runner/`.
It leaves `lib/`, your pubspec config, and every non-Windows platform untouched,
and is fully reversible (`dart run morphic:remove`). **No C++, no CMake, no Win32.**

Full walkthrough → **[doc/QUICKSTART.md](doc/QUICKSTART.md)**.

## Documentation

| Doc | For |
|---|---|
| [QUICKSTART](doc/QUICKSTART.md) | Install → first surface → run, in ~10 minutes |
| [CONCEPTS](doc/CONCEPTS.md) | What a surface/workspace is and *why* Morphic works this way |
| [SDK](doc/SDK.md) | The `package:morphic` API + copy-paste patterns |
| [INTEGRATION](doc/INTEGRATION.md) | How `morphic init` installs the runtime |
| [RUNTIME_MODES](doc/RUNTIME_MODES.md) | Native vs spatial — the two runtime shapes |
| [ARCHITECTURE](ARCHITECTURE.md) | How the runtime works internally |
| [doc/internals/](doc/internals/) | Deep systems investigations (not needed to use Morphic) |

## Status & engineering signals

Pre-1.0 and evolving, but the runtime core is stabilized and the consumption path
is real:

- The install path is **scratch-verified** (init → build → run → remove, on a
  clean `flutter create` app).
- A runtime **invariant assertion layer**, an explicit **lifecycle/teardown state
  machine**, and an **engine-retention** model are all in place and behaviorally
  verified.
- Native and spatial are **separate, verified runtime tiers** (the native tier
  compiles and runs with zero spatial dependencies).

`example/` is a **showcase / playground / reference app**, not the place you
author your own app — that's `package:morphic` in your own project.

> The pre-pivot Dart API (`MorphicController` / `createSurface` / "Studio") is a
> dead lineage, quarantined under `legacy/`. Ignore it.
