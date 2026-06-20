# Success Metrics — Tester #___

**Tester:** ______________  **Date:** __________
**Morphic version:** `0.2.0-dev.___`  **Environment ready (flutter doctor green):** ☐

## Timings

Start each timer when the tester **begins** the task; stop when the
done-criterion is met (or mark **DNF** = did-not-finish/blocked). Use `mm:ss`.

| Metric | Start trigger | Stop trigger | Time | DNF? |
| --- | --- | --- | --- | --- |
| **Time to install** | opens pub.dev page (Task 1) | a Morphic window opens via `flutter run -d windows` |  | ☐ |
| **Time to first surface** | starts Task 2 | their own content shows in the window |  | ☐ |
| **Time to second surface** | starts Task 3 | two windows open simultaneously |  | ☐ |
| **Time to AppBus comms** | starts Task 4 | an action in one window affects the other |  | ☐ |
| **Time to relaunch** | starts Task 5 | app exits cleanly and reopens |  | ☐ |
| **Total session** | session start | session end |  | — |

> Record **environment setup time** (Flutter/VS C++) separately here, *not* in
> "Time to install": **Env setup:** ______ (or "pre-installed").

## Counts

| Metric | Value |
| --- | --- |
| **Docs pages opened** (unique) |  |
| **Times the tester left the docs** (Google / SO / etc.) |  |
| **Interventions required** (from Observer Script) |  |
| **Blocker-severity friction events** |  |
| **Major-severity friction events** |  |
| **Minor-severity friction events** |  |

## Outcome

- **Tasks completed:** ___ / 5
- **Result** (circle one): `Clean pass` · `Pass with friction` · `Assisted` · `Fail`
  - *Clean pass* = 5/5, 0 interventions, 0 Blockers.
  - *Pass with friction* = 5/5, 0 interventions, but Major/Minor logged.
  - *Assisted* = ≥1 intervention.
  - *Fail* = any task DNF.

## Interventions log (if any)

| # | Task | Why intervention was needed | What you said/did |
| --- | --- | --- | --- |
| 1 |  |  |  |
