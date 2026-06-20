# Morphic Cold-Start Validation Kit

A repeatable test: can a Flutter developer who has **never seen Morphic** install
and use it from the **public docs alone**, with **no help**?

Run it with 5 testers per round. Fix the **docs** (not the runtime) until 5/5
reach a clean pass.

| File | Used by | Purpose |
| --- | --- | --- |
| [TESTER_GUIDE.md](TESTER_GUIDE.md) | Tester | The only thing the tester is given: rules + 5 tasks |
| [OBSERVER_SCRIPT.md](OBSERVER_SCRIPT.md) | Observer | How to watch, when (not) to intervene, success criteria |
| [FRICTION_LOG_TEMPLATE.md](FRICTION_LOG_TEMPLATE.md) | Observer | One row per confusion/error (copy per tester) |
| [SUCCESS_METRICS.md](SUCCESS_METRICS.md) | Observer | Timings + counts (copy per tester) |
| [RESULTS_SUMMARY_TEMPLATE.md](RESULTS_SUMMARY_TEMPLATE.md) | Lead | Aggregates all 5 testers → verdict + fixes |

**Per session:** give the tester `TESTER_GUIDE.md` + the pub.dev URL only; the
observer fills a fresh copy of `FRICTION_LOG_TEMPLATE.md` and `SUCCESS_METRICS.md`.
**Per round:** roll the 5 up into `RESULTS_SUMMARY_TEMPLATE.md`.

> This kit is repo-only — it is excluded from the published pub.dev package.
