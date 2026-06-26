# Morphic quickstart

The smallest meaningful Morphic app: **one Flutter app, two real native windows,
kept in sync over `AppBus`.** Window 1 owns a counter; window 2 is a separate
Flutter engine that mirrors it live. Neither window reaches into the other.

It's all in [`lib/main.dart`](lib/main.dart) — ~110 lines, zero Win32.

## Run it (Windows)

From a clone of this repository:

```bash
cd example
flutter create --platforms=windows .   # scaffold the windows/ runner
flutter pub get
dart run morphic:init --apply          # host the Morphic runtime (reversible)
flutter run -d windows
```

Two windows open. Use +/− in **Counter** and watch **Live mirror** update.

## Where to go next

- A fuller, real app: the [Notes Workspace example](../examples/notes_workspace)
  (list · editor · live inspector, with persistence).
- [What is Morphic?](https://www.getmorphic.space/what-is-morphic) ·
  [Docs](https://www.getmorphic.space/docs)

> Uses `morphic` from the repository (`path: ../`). Authored against
> `morphic ^0.2.0-dev.16`; APIs are pre-1.0 and may evolve.
