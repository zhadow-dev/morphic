# Runtime Modes — Native vs Spatial

Morphic ships **two runtime modes**. They are genuinely different runtime shapes
— different window models, lifecycle ownership, and shell semantics — **not** the
same runtime with visual effects bolted on. Choosing a mode is choosing a model,
so it's worth understanding the difference.

## At a glance

| | **Native mode** | **Spatial mode** |
|---|---|---|
| What surfaces are | sovereign OS windows | GPU-composited, shaped, materialized visuals |
| Window model | each surface is a real top-level window | surfaces are composited inside a persistent shell |
| App lifetime | **last surface closes → app exits** | the shell owns lifetime; an empty workspace is valid |
| Shaping / materials | OS window chrome | arbitrary shapes, real blur/acrylic, shadows |
| Tier | free, default | premium |
| Best for | multi-window tools, panels, utilities | spatial canvases, ambient/scene UIs |

## Native mode (default)

The runtime orchestrates a set of **sovereign OS windows**. Each surface is a real
frameless top-level window with its own engine; the runtime owns geometry,
z-order, activation, and lifecycle.

**Lifetime is the classic desktop contract:** the app's root boots your `main()`,
surfaces spawn from there, and **when the last surface closes the app exits.** No
separate "workspace" entity persists beyond the surfaces themselves.

Use native mode for: multi-window editors, inspector/palette layouts, dashboards,
utilities — anything that wants several cooperating real windows.

## Spatial mode (premium)

Surfaces are **adopted into a composited spatial runtime**: their pixels are
GPU-composited as shaped visuals (rounded, capsule, arbitrary geometry) with real
materials (blur/acrylic) and compositor shadows, inside a **persistent shell**.

The shell owns the workspace's lifetime, so the model differs: an **empty
workspace is valid** (you can spawn into it), and the workspace — not "the last
window" — is the unit of app presence. This is what enables ambient, scene-like
UIs where the composition itself is the app.

Use spatial mode for: spatial canvases, ambient presence UIs, scenes where shape
and material *are* the product.

## Why the distinction matters

Because lifecycle ownership differs, the same action can mean different things:

- Closing every surface **exits** a native app, but leaves a **valid empty
  workspace** in a spatial one.
- "The main window" is a concrete root window in native mode; in spatial mode the
  persistent shell carries app identity.

Design for the mode you're in. The SDK you write against is the same
([SDK](SDK.md)); the runtime *shape* underneath is what changes.

## How a surface picks a mode

A surface declares its backend in its `SurfaceSpec`. Native is the default;
spatial surfaces opt in (and require the premium tier). You can mix grounded
native windows and spatial surfaces in one app.

```dart
// native (default)
SurfaceSpec.workspace(id: 'main', entrypoint: 'editor');

// spatial (premium): a shaped, materialized surface
SurfaceSpec(
  id: 'panel', kind: 'tool_palette', entrypoint: 'panel',
  backend: 'spatial', shape: 'rounded', material: 'acrylic', elevation: 18,
);
```

## The premium boundary

Native mode is free and complete. Spatial mode (the GPU compositor) is the
premium tier, delivered separately under a license — the free distribution never
includes the spatial compositor sources. `dart run morphic:init` installs the
native tier; `--spatial` installs the premium tier when licensed. See
[PREMIUM](PREMIUM.md).
