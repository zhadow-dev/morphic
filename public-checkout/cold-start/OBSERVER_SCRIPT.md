# Morphic — Observer Script

Your job is to **watch and record**, not to teach. The session fails if you
help — silence is the product. Read this fully before your first session.

## Before the session

1. **Recruit** a Flutter developer who has never used Morphic and is not a
   Morphic contributor. Note their experience level (years of Flutter, prior
   desktop/multi-window work).
2. **Verify the environment** (this is *not* part of the timed test):
   - `flutter doctor` → the **Windows** check is green (Visual Studio +
     "Desktop development with C++").
   - Record any environment setup time/issues in the friction log, tagged
     `ENVIRONMENT` — these are Flutter-setup friction, reported separately from
     Morphic onboarding.
3. Give the tester **only** `TESTER_GUIDE.md` and the pub.dev URL. Nothing else.
4. Have `FRICTION_LOG_TEMPLATE.md` and `SUCCESS_METRICS.md` open to fill in live.
5. Start a screen recording if the tester consents.

## During the session

- **Stay silent.** Do not answer questions, hint, nod, or react to mistakes.
  If asked "is this right?" respond only: *"Do whatever you'd normally do."*
- **Start the clock** for each task when the tester begins it; **stop** when they
  say "done" (and you've confirmed the done-criterion) or "blocked".
- **Log every friction point as it happens** (see the friction log). Capture the
  *exact* error text and *which doc page* they were on.
- Note every **doc page opened** and every time they **leave the docs** (Google,
  Stack Overflow, etc.).

### When you may intervene (and how to log it)

Only for things **unrelated to Morphic onboarding**. Every intervention is
recorded in `SUCCESS_METRICS.md` as an **intervention** (a session with any
intervention is **not** a clean pass).

| Situation | Allowed? |
| --- | --- |
| Hardware / OS / network failure | ✅ Fix it, tag `ENVIRONMENT` |
| Tester misread a task in the guide | ✅ Re-read the task aloud verbatim, nothing more |
| Tester is stuck >10 min and ready to quit | ✅ Say "you can mark it blocked and move on" |
| Tester asks how Morphic works | ❌ Never |
| Tester hit a doc gap / unclear concept | ❌ Never — **this is the finding** |
| Tester has a compile error | ❌ Never — let them debug from public docs |

## After the session

1. Stop timers; finalize `SUCCESS_METRICS.md`.
2. Ask 3 questions, record verbatim:
   - "Where did you feel most lost?"
   - "What did you expect that didn't happen?"
   - "Would you use this in a real project? Why / why not?"
3. Review the friction log; assign a **severity** to each entry
   (Blocker / Major / Minor — see template).
4. Transfer this tester's row into `RESULTS_SUMMARY_TEMPLATE.md`.

## Success criteria (per tester)

- **Clean pass:** all 5 tasks completed, **0 interventions**, no Blocker-severity
  friction.
- **Pass with friction:** all 5 tasks completed, 0 interventions, but Major/Minor
  friction logged.
- **Assisted:** completed only with ≥1 intervention.
- **Fail:** one or more tasks ended in "blocked".

## Program goal

Iterate the **docs** (not the code) until **5/5 testers reach Clean pass or Pass
with friction** with **zero interventions**. Onboarding is "done" when a stranger
can go from pub.dev to two communicating windows without anyone helping.
