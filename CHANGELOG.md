## 0.2.0-dev.17

* **Fix `flutter run` failing after install with CMake "Cannot find source file:
  src/examples/workflow_patterns.h / No SOURCES given to target: morphic_plugin".**
  The generated `.pubignore` excluded `examples/` **unanchored**, which (by
  gitignore semantics) also stripped the plugin's `windows/src/examples/` sources
  from the published package while `CMakeLists.txt` still referenced them — so the
  plugin couldn't build. The rule is now anchored to `/examples/` (only the
  top-level showcase dir is excluded). No API or runtime changes.

## 0.2.0-dev.16

A deliberately small **stabilization / developer-experience** release. No runtime
changes, no ABI changes, no re-`init` required.

* **Hot restart no longer stacks duplicate windows.** Pressing `R` re-runs your
  `main()`; Morphic now reconciles the previous generation of surfaces, so you
  always end with exactly one running app instead of accumulating windows.
* **Clearer `morphic:init` output** — the apply summary ends with an *Installed
  components* block (Package version, Runtime ABI, Tier) so there's one
  unambiguous place to confirm what's active.
* **Development workflow guide** (`doc/DEVELOPMENT.md`) — explains the
  multi-engine dev loop honestly: hot restart is reconciled; hot reload updates
  the primary isolate but not separately-spawned surface engines (with the
  recommended iterate-then-wire workflow).

## 0.2.0-dev.15

* **Fix `dart run morphic:init --spatial` building with "Cannot open include file
  'compositor_ng/projection_reconciler.h'".** The spatial compositor sources
  (`compositor_ng/`, 19 files) were never vendored into `runtime_assets/`, so they
  were missing from `manifest_spatial.json` and from the delivered runtime ZIP —
  yet the spatial CMake defines `MORPHIC_SPATIAL`, which `#include`s them. They are
  now part of the spatial tier, which is a true superset of native.
* **Spatial install now materializes from the delivered artifact.** `init
  --spatial` previously read from the bundled (premium-stripped) package, which by
  design can never contain `compositor_ng/`. It now extracts the license-gated ZIP
  and materializes the runtime from it. The free package stays compositor_ng-free;
  `manifest_spatial.json` is delivered in the ZIP and no longer shipped to pub.dev.
* The release-integrity gate stays strict on the private tree and the ZIP (both
  manifests, full superset) and is tier-aware for the public package.

## 0.2.0-dev.14

* **`morphic:init` output now removes any doubt about what was installed.** The
  spatial flow is shown as two clearly separated steps — *Step 1: Spatial runtime
  delivery* and *Step 2: project integration* — and the install summary explicitly
  confirms `✓ Spatial runtime <version> installed — active in this project`, so
  it's unmistakable the delivered runtime is the one now hosting the project. The
  version block is aligned (`Package` / `Runtime ABI` / `License` / `Spatial
  runtime`), `Tier` is relabelled `License`, project changes render as a `✓`
  checklist, and the delivery step says the ZIP was "cached" (not "installed",
  which happens in step 2).

## 0.2.0-dev.13

* **Clearer `morphic:init` version output.** `init` printed only
  `runtime version: 0.1.0` (the engine ABI), which read like a downgrade right
  after delivering `spatial runtime v0.2.0-dev.12`. It now leads with the package
  version and labels the engine ABI distinctly (`Morphic 0.2.0-dev.13 (package)` /
  `Runtime engine 0.1.0 (ABI)`). The package version is now carried in the
  manifest (`packageVersion`), sourced from `pubspec.yaml`.

## 0.2.0-dev.12

* **Fix `dart run morphic:init --spatial` failing with "asset hash mismatch for
  windows/runner_morphic/engine_state.h".** `manifest_spatial.json` carried stale
  (pre-line-ending-normalization) checksums for 5 runtime files — the dev.9 fix
  regenerated only the native `manifest.json`. Both manifests are now regenerated
  from the source tree via `tool/gen_manifest.dart`. Native `init` was unaffected.
* **Release-integrity gate** (`tools/verify-manifests.mjs` + CI): every release
  path now fails if any runtime asset hash differs from `manifest.json` **or**
  `manifest_spatial.json` — so a runtime whose files don't match the packaged
  manifest can no longer be synced, zipped, or published.

## 0.2.0-dev.11

Easier to discover, install, and learn. No public API changes.

* **Clear positioning** — Morphic is now described consistently everywhere as
  *a Flutter Desktop runtime for building real multi-window applications in pure
  Dart*: README, pub.dev, the website, and a new **What is Morphic?** page, plus a
  developer FAQ (what it is, why not `desktop_multi_window`, Win32?, platforms).
* **pub.dev metadata** — sharper description; topics now `flutter-desktop`,
  `multi-window`, `window-management`, `desktop`, `runtime`; `homepage` points to
  getmorphic.space with a `documentation` link.
* **Canonical example** — `example/` is now a minimal, runnable two-window +
  `AppBus` quickstart. The fuller showcase lives in `examples/notes_workspace`.
* **Docs corrections** — every install/spatial command uses `--apply` (including
  the `morphic:login`/`morphic:license` CLI hints); commands verified against the
  example.
* **Smaller, cleaner package** — removed internal/validation, quarantined-legacy,
  and generated artifacts from the published package.

## 0.2.0-dev.10

* Docs/onboarding: the README now leads with the **Notes Workspace** example
  (screenshot + clone-&-run) before any architecture, and the Quickstart gains a
  "What you'll build." No code changes.

## 0.2.0-dev.9

* **Fix `dart run morphic:init` failing with "asset hash mismatch … regenerate
  manifest".** The bundled runtime manifest had stale checksums for 5 runtime
  source files, so init aborted before hosting the runtime. Checksums are
  regenerated to match the shipped sources; init applies cleanly again. Found by
  building the `notes_workspace` example from a clean checkout.

## 0.2.0-dev.8

* New `dart run morphic:license` — shows your tier (Developer Preview),
  projects, spatial access and activation status.

## 0.2.0-dev.7

* Fix README onboarding bugs (from a cold-start audit): `morphic:init` needs
  `--apply` to install (without it, it's a dry run); the first-window example now
  includes its `@pragma('vm:entry-point')` function; use the real `.toolPalette`
  constructor and `MorphicSurface.minimize/close`.
* Add the missing **Visual Studio “Desktop development with C++”** prerequisite
  to the README and Quickstart.
* `morphic:init` "Next" hint now uses the correct `runMorphicApp(app: ...)`.

## 0.2.0-dev.6

* CLI now surfaces optional Spatial Mode in `morphic:init`, `morphic:doctor` and
  `morphic --help` — a free Developer Preview pointer (`morphic:login` +
  learn-more link), no paywall or pricing.
* README rewritten as a pub.dev quickstart (install → first window →
  multi-window → AppBus → lifecycle) with a small Spatial teaser.

## 0.2.0-dev.5

* The licensing CLI now defaults to the hosted Morphic backend at
  `https://www.getmorphic.space` (overridable via `MORPHIC_API_URL`). The
  native `dart run morphic:init` is unchanged and needs no backend.

## 0.2.0-dev.4

* Fix: `morphic:login` opens the browser reliably on Windows — PowerShell
  `Start-Process` preserves the `&` in the login URL (the old `cmd start`
  truncated the query string at `&state=`, landing on the wrong page).
* Docs: add the CLI / licensing guide (`doc/CLI.md`). The licensing commands
  (`login` / `whoami` / `logout` / `init --spatial`) are experimental and read
  `MORPHIC_API_URL`; the native `dart run morphic:init` is unaffected.

## 0.2.0-dev.3

* CLI authentication: `dart run morphic:login`, `morphic:logout`, `morphic:whoami`.
* `dart run morphic:init --spatial` securely delivers the spatial runtime — sign
  in, authorize, download over a short-lived signed URL, verify the SHA-256
  checksum, then install. No license keys to copy or paste.
* Browser-based login via a localhost loopback; credentials are stored
  per-platform and access tokens refresh automatically.

## 0.2.0-dev.2

* Moved to a clean public repository with sanitized public history
* Reworked onboarding and documentation structure
* Added `dart run morphic:init` Dart-first workflow
* Re-synced runtime assets with the stabilized native runtime
* Added native/spatial runtime mode separation
* Added runtime invariant enforcement and lifecycle hardening
* Added engine-retention model to avoid Flutter teardown crashes during realistic usage
* Refined install/remove tooling and runtime integration flow
* Cleaned public package boundaries and excluded premium/internal artifacts

## 0.2.0-dev.1

* Reworked public SDK and documentation structure
* Added Dart-first `dart run morphic:init` workflow
* Re-synced runtime assets with stabilized native runtime
* Added native/spatial runtime mode separation
* Added lifecycle hardening, invariant enforcement, and retention model
* Refined onboarding and package structure

## 0.1.0

Initial public release.

### Features
- Native multi-surface Flutter desktop runtime
- `dart run morphic:init` Windows runtime installer
- Native and spatial runtime modes
- Engine retention lifecycle model
- Multi-window orchestration APIs