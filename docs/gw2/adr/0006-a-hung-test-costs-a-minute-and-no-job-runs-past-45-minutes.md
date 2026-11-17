# A hung test costs a minute, and no job runs past 45 minutes

Decided 2026-11-17. Status: accepted.

Run 43452320908 took 69 minutes and 92 runner minutes to say what it knew after ten. Its Linux job
ran FEX's assembly tests with `FEX_ENTRYHEADERLOOKUP=1`, 45 of them never ended, and ctest waited
302 s for each, four at a time: 3,709 s for one pass. The slowest test of a healthy pass takes 8.7
to 10.4 s, in that run and in run 43769470898, and all but 14 of the 7,665 results I parsed there
take under a second. The two jobs' own limits were 150 minutes. The longest green macOS job of the
26 runs before took 20.0 minutes (run 43659980437), the longest red one 23.2, the longest green
Linux job 10.7. Those limits bounded nothing.

The macOS stage had the opposite problem in one case. `hang.exe` prints one line and sleeps for
good, so `hang` passes only when its limit fires, in every run. At 20 s the case took 40 to 52 s:
20 s of waiting and 20 to 32 s of sampling. Mach9's smoke program, which does more than print a
line, starts and ends in 1 to 5 s in the same prefix.

So the limits are now ones a healthy run never meets and a broken one meets soon:

- ctest's `--timeout` in the `core` job is 60 s, six times the slowest test.
- The `build` job stops at 45 minutes, the `core` job at 30.
- `hang` runs for 10 s before the watchdog samples it.

## Consequences

- Run 43452320908's Linux job would have ended after about 21 minutes instead of 69. 45 hung tests
  at 60 s, four at a time, is 11 minutes for that pass.
- A green run saves 10 s, the `hang` case's share. The rest of the saving is on red runs.
- No test in `unittests/` sets a `TIMEOUT` of its own (checked with `git grep`), so `--timeout` is
  every test's limit. A test that adds one escapes it.
- A healthy assembly test that runs past 60 s on a slow runner fails as `***Timeout`. Then I look
  at that test before the number goes up, and only I raise it.
- A run with an empty ccache compiles all 418 objects of the engine's commit instead of none. That
  costs 7 to 9 minutes more, and the macOS job takes about 28. It happens after an engine move or a
  new llvm-mingw or Xcode. 45 minutes covers it.
- 30 minutes is three times today's `core` job. A fifth assembly pass needs a check against it.
- If `hang.exe` doesn't reach its sleep in 10 s, the case fails with `slept no`. That is a finding
  about process start to report, not a reason to give the case its 20 s back.
- The manual jobs, `budget` and `stress`, keep their own limits.
- The test stage in two lanes (plan 017) lands after this change has had a green run, so the
  10 s limit and the lanes don't meet for the first time in one run.

## Alternatives I rejected

- **Shorter limits on the other cases in `tests/gw2/cases.zsh`.** They only bound a failure. A limit
  that fires on a slow runner costs a run.
- **Stopping the stage at its first failed case.** I rejected that for my Wine fork already: it
  hides the later failures, and with one push per go I want to see all of them.
- **A macOS job limit under 45 minutes.** The cold-ccache run at about 28 minutes and a stage with
  several hung cases at 23 would come too close to it.

## Evidence

- jdrly/gw2-on-mac #233.
- Runs 43452320908 (the 69-minute run), 43769470898 (the healthy passes' times) and 43659980437
  (the longest green macOS job).
- GW2 on Mac workspace: `advisor-plans/2026-11-17-ci/016-fex-ci-time-limits.md` and that folder's
  `REVIEW.md` ("The step without a useful limit", and the `hang` row of the case table).
