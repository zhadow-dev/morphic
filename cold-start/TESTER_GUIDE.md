# Morphic — Tester Guide

Thanks for evaluating **Morphic**. You're playing the role of a Flutter
developer who has **never seen it before**. We're not testing *you* — we're
testing whether the docs let you succeed **on your own**.

## Ground rules

- Use **only** what's publicly available: the pub.dev page
  **https://pub.dev/packages/morphic** and the docs it links to.
- **Do not** ask anyone for help, search Discord/Slack, or read the package
  source. If you'd normally Google an error, that's allowed — note it.
- **Think aloud.** Say what you're looking for, what confuses you, what you
  expect to happen. The observer is silent; your narration is the data.
- If you get stuck for **more than 10 minutes** on one step, say
  *"I'm blocked"* and move on (or stop). Being blocked is a valid result.
- There are **no trick questions** and no "right" way — do what a real dev
  evaluating a package would do.

## Before you start (environment)

This is a **Windows desktop** package. The observer will confirm your machine is
ready first: `flutter doctor` should show the **Windows** check green (this needs
Visual Studio with the "Desktop development with C++" workload). If that isn't
green, the session pauses until it is — that setup time is recorded separately
and is **not** part of the Morphic evaluation.

## Your tasks

Do these in order. After each, tell the observer **"done"** (or **"blocked"**).

### Task 1 — Install Morphic
Get Morphic into a fresh Flutter app and run it once.
**Done when:** a Morphic-hosted desktop window opens via `flutter run -d windows`.

### Task 2 — Create your first surface
Make that window show your own content (any text/widget you like).
**Done when:** your custom content appears in the window.

### Task 3 — Create a second surface
Make the app open a **second** window with different content.
**Done when:** two separate windows are open at the same time.

### Task 4 — Send data through AppBus
Make one window send a value to the other (e.g. click a button in window A and
see something change in window B).
**Done when:** an action in one window visibly affects the other.

### Task 5 — Close and relaunch
Close the app completely, then run it again.
**Done when:** the app exits cleanly and relaunches to its starting state.

## When you finish

Tell the observer you're done. They may ask a few short questions about what was
confusing. Be blunt — confusion is exactly what we want to capture.
