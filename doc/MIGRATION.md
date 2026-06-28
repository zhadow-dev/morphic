# Migrating to Morphic 0.2 (ABI 0.2)

Morphic 0.2 makes the runtime **headless**. This is the foundation release —
ABI 0.2 — and it changes one thing you can see and one line you write. Your app
code (surfaces, entrypoints, AppBus) does **not** change.

## What changed

- **There is no launcher window anymore.** Previously the runtime owned a small
  root/launcher window. Now your app's `main()` runs on a *headless* engine and
  **every visible window is one of your Surfaces**. The process exits when the
  last Surface closes.
- **Engine ABI moved 0.1.0 → 0.2.0.** Existing projects must re-materialize the
  runtime once (one command, below).
- **`MorphicRuntime.run(app: ...)` is the entrypoint.** `runMorphicApp(app: ...)`
  still works — it is now a thin deprecated shim, so you can upgrade without
  touching `main()` if you prefer.
- **`MorphicApp.dissolveRootIntoScene` was removed.** It only existed to dissolve
  the old launcher root. Delete any override — there is no root window to dissolve.

## Upgrade steps

From any `0.2.0-dev.x` (ABI 0.1) project:

```bash
# 1. take the new package
flutter pub upgrade                 # or set: morphic: ^0.3.0-beta.1 in pubspec.yaml

# 2. re-materialize the runtime (ABI 0.1 -> 0.2)
dart run morphic:init --force

# 3. run
flutter run -d windows
```

`morphic:init` is now **version-aware**. If you run it without `--force` on an
older install it tells you an upgrade is available rather than silently doing
nothing:

```
Installed runtime:

  ABI 0.1.0

Available runtime:

  ABI 0.2.0

A runtime upgrade is available.

Re-run with:

  dart run morphic:init --force
```

## Optional: adopt the new entrypoint

```dart
// before (still works)
void main() => runMorphicApp(app: MyApp());

// after (recommended)
void main() => MorphicRuntime.run(app: MyApp());
```

## What you do NOT need to change

- `MorphicApp` / `SurfaceSpec` declarations.
- Your `@pragma('vm:entry-point')` surface entrypoints.
- `AppBus` topics/payloads, `MorphicSurface`, `EcologyController`.

## Notes

- The runtime is frozen at ABI 0.2 (see
  `doc/internals/runtime/RUNTIME_CORE_INVARIANTS.md`). Future releases keep this
  contract; another `init --force` is only needed when the Runtime ABI changes.
- `remove` then `init` is equivalent to `init --force` if you prefer a clean
  re-materialization.
