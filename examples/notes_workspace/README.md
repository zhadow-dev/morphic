# Notes Workspace — a real multi-window Morphic app

Three **separate windows** — a notes **List**, an **Editor**, and a live
**Inspector** — that are one Flutter app, talking over `AppBus`, with edits that
**persist across relaunch**. ~250 lines of Dart, zero Win32.

This is the example to clone first: it shows *why* Morphic exists in software you
could actually use.

## Run it (Windows)

Prerequisites: Flutter with Windows desktop enabled, and **Visual Studio** with
the **“Desktop development with C++”** workload (`flutter doctor` → Windows green).

```bash
git clone <this repo>
cd examples/notes_workspace
flutter create --platforms=windows .   # scaffold windows/runner around this lib/
flutter pub get
dart run morphic:init --apply          # host the Morphic runtime (reversible)
flutter run -d windows
```

Three windows open and the most recent note is already loaded. Pick a note in the
List → it opens in the Editor and the Inspector updates live. Type → it autosaves.
Close the app and reopen — your notes are still there.

## Use it like real software

| Action | How |
| --- | --- |
| New note | `Ctrl+N` or the **+** button |
| Rename | `F2`, or double-click a row |
| Delete | `Del` (with an **Undo** snackbar) or the trash icon on hover |
| Move selection | `↑` / `↓` |
| Save now | `Ctrl+S` (it also autosaves while you type) |
| Close the editor window | `Ctrl+W` |

> **One honest rough edge:** `Ctrl+N` focuses the Editor's title field, but the
> Editor is a *separate OS window* — so on Windows you click it once before typing
> the new title. Cross-window activation is an SDK gap, not an app bug; it's on the
> backlog precisely because using the app surfaced it.

## What it demonstrates

| Feature | Where |
| --- | --- |
| **Multiple windows** | `NotesApp.surfaces()` declares list + editor + inspector ([lib/main.dart](lib/main.dart)) |
| **AppBus communication** | `note.selected` / `notes.changed` topics ([lib/surfaces.dart](lib/surfaces.dart)) |
| **Surface lifecycle** | the Editor closes its *own* window via `MorphicSurface.close()` |
| **Persistence** | a shared JSON file is the source of truth ([lib/store.dart](lib/store.dart)) |

## The one idea

Surfaces are isolated engines — they never reach into each other. They
**persist to a shared store and broadcast over `AppBus`**; each window reloads
on the events it cares about. That single rule is the whole multi-window model.

## Files

```
lib/
  main.dart       # entrypoints + the MorphicApp that declares the 3 windows
  surfaces.dart   # the List / Editor / Inspector widgets
  store.dart      # Note model + file persistence + AppBus broadcasts
```

> Authored against `morphic ^0.2.0-dev.8`. APIs are pre-1.0 and may evolve.
