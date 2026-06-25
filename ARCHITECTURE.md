<!--
  PUBLIC architecture doc. Authored here in morphic-private; the release tool
  mirrors it to morphic-public as ARCHITECTURE.md.
  Keep it a selling point: layering, topology, lifecycle, install model.
  NO forensics, NO premium implementation details — those live in the full
  ARCHITECTURE.md and doc/internals/ (private only).
-->
# Morphic — Architecture

How Morphic turns one Flutter app into a runtime of real, sovereign native
windows — orchestrated for you, authored entirely in Dart.

## The surface model

A **surface** is a real OS window. Each surface runs **its own Flutter engine**,
so windows are genuinely independent — separate widget trees, separate rendering,
one process. You don't manage `HWND`s, message loops, or Win32; you declare what
surfaces exist and what each one renders, and the runtime does the rest.

```
runMorphicApp(app: MyApp)
        │
        ▼
   MorphicApp.surfaces()  ──►  [ workspace · inspector · toolPalette · overlay ]
                                  each a named @pragma('vm:entry-point')
```

## A layered runtime

The runtime is built as a sequence of clean, swappable boundaries. Each layer has
one job and hands a well-defined contract to the next:

| Layer | Responsibility |
| --- | --- |
| **Semantic** | The source of truth: what surfaces exist and how they relate (groups, ownership, z-order intent). |
| **Interaction** | Resolves gestures — drag, group, dock, extract — deterministically. |
| **Presentation** | Keeps the semantic truth and the on-screen windows coherent (settling, smoothing). |
| **Projection** | Projects geometry and z-order onto real OS windows. |
| **Native** | The platform shell — Win32 today; more to come. |

Because the boundaries are explicit, the platform layer can change (or a GPU
compositor can be slotted under projection) without rewriting the layers above.

## Topology, ownership & z-order

Surfaces aren't a flat list — they form a **topology**:

- **Groups** — surfaces that belong together move together.
- **Ownership chains** — a panel (inspector, palette) is *owned* by its host
  window: it raises, minimizes, and travels with the host, and stays off the
  taskbar/Alt-Tab like a native utility.
- **Deterministic z-order & activation** — the runtime owns the semantic order
  and projects it onto the OS so stacking and focus stay coherent across windows,
  including owner-aware ordering (owned windows above their owner).

## Communication: AppBus

Surfaces never reach into each other. They **persist to a shared store and
broadcast over `AppBus`**; each surface reloads on the topics it cares about. That
single rule is the whole multi-window data model — see the
[Notes Workspace example](examples/notes_workspace).

## Lifecycle

The runtime publishes lifecycle events on the same bus; a surface can drive **its
own** window (`MorphicSurface.minimize / maximize / close`) but never another's.
In **native** mode the app follows the classic desktop contract — when the last
window closes, the app exits.

## Install architecture

Morphic wraps an *existing* Flutter desktop app; it doesn't replace it.

```
dart run morphic:init --apply
   └─ patches windows/runner to host the Morphic runtime
      • leaves lib/ and your pubspec untouched
      • leaves every non-Windows platform untouched
      • fully reversible:  dart run morphic:remove --apply
```

There's no C++, CMake, or Win32 for you to write — the runtime ships as hosted
runner sources that `init` installs and a checksum manifest verifies.

## Two runtime tiers

- **Native** (default, free): sovereign frameless OS windows, orchestrated. The
  default, available today, no account or network required.
- **Spatial** (experimental Developer Preview): surfaces are composited on a GPU
  plane as shaped, materialized visuals inside a persistent shell. See
  [Spatial](https://www.getmorphic.space/spatial).

## Platform support

Windows today. macOS and Linux are on the roadmap — the layered design exists
precisely so the upper layers carry over when new platform shells land.

---

*Using Morphic? You don't need this document — start with the
[Quickstart](doc/QUICKSTART.md). This is the architecture overview for evaluators
and contributors.*
