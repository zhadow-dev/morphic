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