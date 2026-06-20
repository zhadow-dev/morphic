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