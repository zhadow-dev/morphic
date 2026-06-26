# Development workflow

Morphic is a **multi-window, multi-engine** runtime: each surface (window) is its
own Flutter engine with its own isolate. That's what makes real OS windows
possible — and it means the `flutter run` dev loop behaves a little differently
than a single-window app. This page tells you exactly how, so nothing surprises
you.

## Running

```bash
flutter pub get
flutter run -d windows
```
Your app's surfaces open as separate native windows.

## Hot restart (`R`) — reconciled

Press **`R`** and you get exactly **one** running app. A hot restart re-runs your
`main()`, and Morphic reconciles the previous generation of windows so they don't
stack up. (Before this was added, repeated restarts could leave duplicate
windows — that's fixed.)

## Hot reload (`r`) — does not reach spawned surfaces yet

`flutter run`'s hot reload is attached to your app's **primary** isolate. Because
each surface runs in its **own** engine, editing a surface's UI and pressing `r`
does **not** repaint that surface. This is an inherent property of the
multi-engine model today, not a bug — and there's no surface-level hot reload
yet.

**Recommended workflow while iterating a surface's UI:** build it as an ordinary
top-level app first, where hot reload works normally —
```dart
void main() => runApp(const MaterialApp(
      home: Scaffold(body: Center(child: Text('Hello, Morphic!'))),
    ));
```
— get the UI right with fast `r`, then switch `main` back to
`runMorphicApp(app: MyApp())` and wire it as a surface entrypoint. For a quick
refresh of already-wired surfaces, stop and re-run (`q` then `flutter run`).

## The small "presence" window

You may see a small window with your app's name. It's the app's bootstrap
presence — the engine that runs your `main()` and spawns your surfaces. It's a
runtime detail; treat your real workspace/surfaces as your app's UI.

## Coordinating between windows

Surfaces never reach into each other. They share **data** over `AppBus`:
```dart
AppBus.broadcast('note.selected', {'id': id});         // any surface
AppBus.on('note.selected', (p) => open(p['id'] as String));
```
`AppBus` carries data only — never input or focus. The OS-focused window owns the
keyboard. A surface drives only its own window via `MorphicSurface`
(`minimize`/`maximize`/`close`/…).

## Quick reference

| Action | Behavior |
|---|---|
| `flutter run -d windows` | surfaces open as native windows |
| `R` (hot restart) | one app, reconciled — no duplicate windows |
| `r` (hot reload) | updates the primary isolate; **not** spawned surfaces |
| iterate a surface's UI | develop as a standalone `runApp` app, then wire it |
| cross-window sync | `AppBus.broadcast` / `AppBus.on` (data only) |
