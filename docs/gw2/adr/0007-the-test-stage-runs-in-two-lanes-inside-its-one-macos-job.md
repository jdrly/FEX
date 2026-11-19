# The test stage runs in two lanes inside its one macOS job

Decided 2026-11-17. Status: accepted.

`Tests on the engine` ran its cases one at a time on a runner with 3 CPUs. It took 671 to 724 s in
the four runs up to run 43769470898, the two older ones with 78 and 68 cases where it has 89 now,
and 594, 642 and 797 s in the three pull request runs of 2026-11-17. Eleven cases, those of
`counters.exe` and `codebuf.exe`, took 306 to 352 s of the first four, 46 to 49% of the stage,
each on one thread. In run 43769470898 they took 192 and 114 s, and every other case under my
FEX 205 s.

So the stage now runs as two `engine.zsh` processes side by side in the same step. `GW2_LANE=long`
runs those eleven under my FEX, in the prefix `mine-long`, with a second copy of my engine.
`GW2_LANE=short` runs every other case, under both FEXes, in the prefixes the stage used before.
With `GW2_LANE` unset, `engine.zsh` runs every case in one process, as it did.

The first run with lanes, run 44016265366, took 500 s for the step. That is 150 to 214 s less
than the 650 to 714 s I planned against, and less than any of the three runs before it on the same
day. The long lane's eleven cases took 453 s of program time, against 381 s in a row in run
44012662403. The short lane's timed cases took 279 s, against 233 s. Side by side on 3 CPUs, both
ran about 20% slower than in a row. I had planned on 336 and 380 s for the lanes and a step under
480 s. That target was an estimate from run 43769470898's times, made before any run with lanes.
I keep the 500 s. A job of its own for the long lane would take another runner and a second
setup.

## 2026-11-19: the reruns fold into two sets

jdrly/gw2-on-mac#325. In run 44433755065 the step took 487 s. The file times in its
`engine-tests` artifact put the short lane at about 486 s and the long lane at about 466 s, so
both lanes set the step's length. Of its 89 cases, the reruns of nine programs under four sets
of my switches (`lookup-off`, `compilelock`, `cuts`, `dev`) took about 315 s of program time,
and smaller reruns about 80 s more. The CI audit of 2026-11-19 found no run in which one of
them failed.

They are two sets now, at the end of `cases.zsh`. `dev` has each of my on-or-off switches on,
and `engine-like` has both lookups at 0. Every program that had a rerun runs once under each.
`engine-like` is what players run: a release build of my app runs the engine's FEX. The three
cases with only the dispatcher's L1 lookup off stay. That leaves 71 cases, in the same two
lanes as before.

Predicted from run 44433755065's case times, with each new case timed as the old rerun it
replaces: the long lane about 395 s, the short lane about 410 s, the step about 410 s. That is
a prediction, not a measured run. The issue asked for under 300 s. Its own list doesn't get
there. The kept cases plus one boot per prefix come to about 805 s of lane time, so two lanes
can't go much under 400 s. I keep these cases rather than cut more to reach 300.

Run 44433755065's step summary didn't upload. GitHub takes 1024k, and the step wrote 1220k,
about 172k of it for each `counters` or `slots` case, all reads.
`stats` now prints each read's first 20 lines. The whole files are in the artifact. Each lane's
summary ends with a table of its cases, with each set named.

## Consequences

- Every case still runs once, except `smoke-log` and its report `hardware-tso`. `counters_check`
  reads that report from its own prefix, so the long lane runs them too, 2 to 3 s.
- Each lane needs an engine directory of its own. `run.zsh` finds a run's processes by the path
  of the engine they run from and kills what is left after the run, so with one copy each lane
  would kill the other's processes. The copy is an APFS clone, as the first one is.
- A check in `cases.zsh` that reads a case's logs returns when its lane didn't run the case, by
  the `ran` list `engine.zsh` keeps. `hang_check` and `crash_check` call `run.zsh` themselves and
  ask `in_lane` first. A new check needs the same line, and a stand-in for `run.zsh` that starts
  nothing shows a check that forgets it: plan 017's `lanes-check.zsh`.
- A pull request opened before this change meets the new workflow with its old `engine.zsh`,
  because `tests/gw2` comes from the pull request's head. The workflow looks for `GW2_LANE` in
  the `engine.zsh` it has and runs it once when it isn't there.
- The cases share 3 CPUs with one more busy thread. No case passes on a speed, and the limits are
  30 to 120 times a healthy case. `tsoqueue` counts how many items arrive in its 10 s, so it now
  counts fewer: 39,087,034 in run 44016265366, against 99,350,136 in run 44012662403. Nothing
  fails on that.
- In run 44016265366, before the two sets, the two lanes added up to about 930 s of work.
  Moving a case from one lane to the other couldn't bring the step reliably under 480 s then.
- The summary has the long lane's block last, after "Reports".
- The `build` job's 45 minutes and `hang`'s 10 s from ADR 0006 stay. The lanes landed after that
  change had a green run, run 44012662403.

## Alternatives I rejected

- **A second macOS job for the long lane.** It needs a second setup and a second runner: about
  13.2 minutes of wall clock and 3 more macOS minutes than one job, by the plan's estimate. The
  lanes took 2.5 to 3.5 minutes off the stage in run 44016265366, in the one job. The workflow keeps the stage in the build job so it stays one macOS job at a time. It is
  my fallback if two lanes don't hold on 3 CPUs, with the same `GW2_LANE`.
- **More than two lanes.** The runner has 3 CPUs, and the short lane already starts several
  threads in some cases.
- **The cases in a row, as before.** That is the 10 to 13 minutes this change cuts down.

## Evidence

- jdrly/gw2-on-mac #235, and #325 for the two sets.
- Run 44433755065 and its `engine-tests` artifact for the lane times before the two sets.
- Run 43769470898 for the cases' times, and the job's first step for the runner's 3 CPUs. Runs
  43994927356, 44009162480 and 44012662403 for the three latest steps in a row. Run 44016265366
  for the first one with lanes.
- GW2 on Mac workspace: `advisor-plans/2026-11-17-ci/017-fex-ci-two-lanes.md` and that folder's
  `REVIEW.md`.
