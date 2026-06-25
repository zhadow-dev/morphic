# Integration — how `morphic init` installs the runtime

Morphic isn't a normal pub plugin: a normal plugin is a *guest* in Flutter's
process, but Morphic **hosts** Flutter (it owns process bootstrap and the window
loop). You can't make a host out of a guest just by listing a dependency — so a
small **runner transformer** does the inversion, exactly like `flutterfire
configure` / `tauri init` / Expo `prebuild`.

```bash
flutter pub add morphic
dart run morphic:init --apply
```

## What `init` does

- **Adds** the `morphic` dependency (if missing).
- **Materializes** the runtime sources into `windows/runner/` (they ship as
  package assets and are written into your project — your app owns its runtime
  instance, like Expo prebuild).
- **Replaces** `windows/runner/main.cpp` (hands process bootstrap from Flutter's
  default window to the Morphic runtime) — your original is backed up.
- **Patches** `windows/runner/CMakeLists.txt` inside a fenced block (adds the
  runtime sources, C++20, the needed Windows libs) — additive, never rewriting
  Flutter's generated parts.

It **leaves untouched**: `lib/`, your app's pubspec config, and every non-Windows
platform.

## Reversible and idempotent

```bash
dart run morphic:init           # dry run — prints the plan, changes nothing
dart run morphic:init --apply   # performs it
dart run morphic:remove --apply # reverses it, restoring your files byte-for-byte
dart run morphic:doctor         # read-only diagnostics
```

Every mutation is journaled before it happens (crash-recoverable), backups are
kept, and re-running `init` is a no-op (a single fence). If any step fails, it
rolls back automatically. The whole loop — init → build → run → remove → vanilla
— is verified on a clean `flutter create` app.

## What you own afterward

The runtime sources now live in *your* `windows/runner/` and are yours to commit.
Flutter's plugin machinery wires the `morphic` plugin shell automatically from the
pubspec dependency (symlink + registrant), so there's nothing else to hand-edit.

## Tiers

`init` installs the **native** tier by default. The **spatial** (premium) tier —
the GPU compositor — installs with `--spatial` when you have an activated license;
its sources are delivered separately and never ship in the free distribution. See
[RUNTIME_MODES](RUNTIME_MODES.md) and [PREMIUM](PREMIUM.md).

## Updating

Re-run `dart run morphic:init --force` to reinstall a newer runtime version (it
reverses the prior install first, then installs fresh). Your `lib/` and app code
are untouched.
