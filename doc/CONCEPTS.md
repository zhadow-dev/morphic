# Concepts

This page explains *why* Morphic is shaped the way it is — the mental model. It
is not a how-to (see [QUICKSTART](QUICKSTART.md)) and not internals (see
[ARCHITECTURE](../ARCHITECTURE.md)).

## Why Morphic exists

A Flutter desktop app is **one window, one widget tree**. That's perfect for most
apps and a hard ceiling for some: tools that want multiple real windows working
together — an editor with detached inspectors, a workspace of companion panels, a
spatial canvas — can't express that as a single widget tree, and hand-rolling
multiple native windows + engines is deep Win32 work.

Morphic is that capability, as a package: **one app, many sovereign windows, all
authored in Dart.**

## Surface

A **surface** is the core unit: *a window is an engine is a surface.* Each surface
is a real top-level OS window running its **own Flutter engine** with its own
widget tree. Surfaces don't nest into a parent tree — they are independent.

You describe a surface with a `SurfaceSpec` (what to spawn and how it relates),
and its content is an ordinary Flutter app behind a named entrypoint. The spec
*describes*; it never renders the surface inline.

## Why sovereign windows

Each surface being its own engine is the deliberate design, not a limitation:

- **Real windows.** They minimize, maximize, alt-tab, and live on the taskbar
  like any native window — because they are native windows.
- **Isolation.** A surface can't reach into another's state, input, or focus. That
  makes multi-window apps predictable instead of a tangle of cross-window wiring.
- **The keyboard follows focus.** Input is owned by the OS-focused window. You
  never route keystrokes between surfaces — there's nothing to get wrong.

The cost is the honest one: surfaces **cannot share Dart state** (separate
engines). They coordinate **data** — never input — over the [AppBus](SDK.md#appbus).

## Orchestration (and what Morphic does *not* do)

The runtime **orchestrates** surfaces: it owns their geometry, z-order,
activation, and lifecycle, and spawns/destroys them on request. That's why you
declare surfaces and call `spawn`/`close` instead of managing HWNDs.

Morphic deliberately is **not** a window manager: no docking, tiling, snapping,
or layout solving. It positions what you author; it never owns your layout.

## Workspace

A **workspace** is a grounded, taskbar-valid root surface — the natural "main
window" of an app, and the thing other surfaces relate to. Support surfaces
(inspectors, tool palettes) declare a `parent` workspace. In spatial mode a
workspace also anchors a composition; in native mode it's simply the root window.

## Native vs spatial — two runtime shapes

Morphic runs in one of two modes, and they are **genuinely different runtimes**,
not one runtime with optional effects:

- **Native mode** — sovereign OS windows orchestrated by the runtime. Lifetime is
  the classic desktop contract: when the last surface closes, the app exits.
- **Spatial mode** — surfaces become GPU-composited, shaped, materialized visuals
  inside a persistent shell that owns the workspace's lifetime. The window model,
  shell semantics, and lifecycle ownership differ.

This matters when you reason about lifetime and behavior. Full treatment:
[RUNTIME_MODES](RUNTIME_MODES.md).

## How this differs from normal Flutter desktop

| | Normal Flutter desktop | Morphic |
|---|---|---|
| Windows per app | one | many (sovereign) |
| State | one shared tree | per-surface engines; data via AppBus |
| Window control | the framework's single window | runtime-orchestrated, per-surface |
| Authoring | widgets | widgets **+** a small orchestration layer (specs) |

You still write Flutter. Morphic adds the layer *above* the widget tree —
which surfaces exist, how they relate, and how they're orchestrated.
