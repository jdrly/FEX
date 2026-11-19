# A docs-only pull request runs no test, and two assembly passes run on pushes and Core changes

Decided 2026-11-19. Status: accepted.

The Linux job runs FEX's 7,695 assembly tests four times, 377 of its 398 s. Two of the four
passes, `asm-l1lookup-off` (the dispatcher's L1 lookup off) and `asm-cuts` (#164's two
switches), have failed exactly as the default pass did in every run since they exist: the same
18 tests. The CI audit of 2026-11-19 put them at about 190 s of a pull request's Linux job.

My fork also had no selection by what changed. Pull request #20 changed six Markdown files and
nothing else. Its run 44102293640 still paid 580 s for the build job, 406 s of them in the test
stage, and 451 s for the Linux job.

So the step "Which tests this run owes", in the build job and the core job, now also reads a
pull request's changed files, from GitHub's compare of its base with its head:

- When every changed file is Markdown or under `docs/`, the build job builds, compares and runs
  Mach9's smoke program once under my FEX, and the core job builds and runs no test. One other
  file of any kind, the workflow and the test scripts included, and the pull request runs its
  tests.
- When it changes nothing under `FEXCore/Source/Interface/Core` and not `gw2.yml`, the core job
  runs its tests without `asm-l1lookup-off` and `asm-cuts`. The reference comparison then leaves
  out the `asm-cuts` lines too. A Markdown file under Core counts as a change to Core.
- Every push runs all of the core job's tests, the four passes among them. That includes a push
  whose tree a green pull request tested: its build job still runs only the smoke program in
  place of the test stage (jdrly/gw2-on-mac#226, #242), but its core job no longer skips.

## Consequences

- A docs-only pull request's run should take about as long as push run 44435913533, which took
  the same path: 4:50, of which 4:30 for the build job and 1:43 for the core job, side by side.
  That is under the issue's 5 minutes only on a runner as fast as that one. Building and
  comparing stay, so a slow runner can go past it.
- A push whose tree a pull request tested takes longer than before. Its core job now runs about
  7 minutes (7:05 in run 44631304080) beside the build job's 4:30, so the run takes about 7:15
  where push run 44435913533 took 4:50. It runs on GitHub's Linux runner, not the macOS one that
  holds the other runs up.
- A pull request that changes Core or the workflow pays what it paid before. One that changes
  `tests/gw2`, `Source/Windows` or the rest of FEXCore runs two passes fewer, and the push that
  merges it runs them.
- The two passes guard switches whose code is under `FEXCore/Source/Interface/Core`
  (`Dispatcher/` and `OpcodeDispatcher.cpp`). Their entries in `Config.json.in` and `Context.h`
  are outside it, so a pull request that changes only those runs without the two passes, and
  its merge push runs them.
- No build step or test reads a Markdown file in this tree, and every file under `docs/` is
  Markdown. `FEXCore/Source/CMakeLists.txt` writes `IR.md` into the build directory and reads
  none. A Markdown file that a test reads would need its own exception.
- Any doubt runs everything: a lookup that fails, an empty list, or a list at the compare's cap
  of 300 files (checked against a 1,007-file diff, which it cut to 300). The step summary says
  what it decided.

## Alternatives I rejected

- **Workflow changes that touch only comment lines count as docs, as in my Wine fork's ADR
  0015.** A `#` line after a shell line's trailing backslash ends that command, so a change the
  filter calls a comment can break a step. Every change to `gw2.yml` runs the tests.
- **A push whose tree a green pull request tested skips the core job's tests, as before.** A
  pull request that skipped the two passes would then have them run nowhere.
- **The selection with `git diff`.** The core job checks out one commit without history, and
  fetching the history would add to every run. The compare API gives the same list in one call,
  before either job checks anything out.
- **Dropping the two passes altogether.** They are cheap on a push, and the dispatcher's L1
  lookup is a default since ADR 0008.

## Evidence

- jdrly/gw2-on-mac #326.
- Run 44102293640 (pull request #20, Markdown only) for what a docs-only pull request paid. Push
  run 44435913533 for the same path without the tests, run 44631304080 for the core job's 7:05.
- GW2 on Mac workspace: `advisor-plans/2026-11-19-ci-audit/AUDIT.md`, recommendation 12.
- The step's script, run on this server against pull requests #18 to #23, the push of
  `9b0d320f5`, commit `502a1900e` (the dispatcher alone), a 1,007-file diff, a head GitHub
  doesn't have and a repository that doesn't exist. #20 came out docs only, #23 with two passes
  fewer, #18, #19, #22 and `502a1900e` with all four, every failed lookup as every test.
