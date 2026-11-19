# A docs-only pull request runs no test, and two assembly passes run on pushes and Core changes

Decided 2026-11-19. Status: accepted.

The Linux job runs FEX's 7,695 assembly tests four times, 377 of its 398 s. Two of the four
passes, `asm-l1lookup-off` (the dispatcher's L1 lookup off) and `asm-cuts` (#164's two
switches), have failed exactly as the default pass did in every run since they exist: the same
18 tests. The CI audit of 2026-11-19 put them at about 190 s of a pull request's Linux job.

My fork also had no selection by what changed. Pull request #20 changed six Markdown files and
nothing else. Its run 44102293640 still paid 580 s for the build job, 406 s of them in the test
stage, and 451 s for the Linux job.

So the step "Which tests this run owes", in the build job and the core job, now also looks at a
pull request's changed files, from GitHub's compare of its base with its head:

- When every changed file is Markdown, or `gw2.yml` with only comment lines changed, the pull
  request runs as a push whose tree a green pull request tested: the build job builds, compares
  and runs Mach9's smoke program once under my FEX, and the core job builds and runs no test.
- When it changes nothing under `FEXCore/Source/Interface/Core` and no line of `gw2.yml` but
  comments, the core job runs its tests without `asm-l1lookup-off` and `asm-cuts`. The reference
  comparison then leaves out the `asm-cuts` lines too.
- A push runs all four passes whenever it runs the core job's tests. A push whose tree a green
  pull request tested still runs none (jdrly/gw2-on-mac#226, #242).

## Consequences

- A docs-only pull request's run should take about as long as push run 44435913533, which took
  the same path: 4:50, of which 4:30 for the build job and 1:43 for the core job, side by side.
  That is under the issue's 5 minutes only on a runner as fast as that one. Building and
  comparing stay, so a slow runner can go past it.
- A pull request that changes Core or the workflow pays what it paid before. One that changes
  `tests/gw2`, `Source/Windows` or the rest of FEXCore runs two passes fewer.
- The pull request that brought this changes the workflow, so its own run has all four passes.
  The push that merges it then finds that green run and runs only the smoke program. The first
  push after it that owes its tests is the one that shows all four on a push.
- A comment line is one whose first character after the indentation is `#`. That holds because
  `gw2.yml` has no here-document and no multi-line string with a line that starts with `#` as
  data. One that adds such a line has to change the rule with it.
- No build step or test reads a Markdown file in this tree. `FEXCore/Source/CMakeLists.txt`
  writes `IR.md` into the build directory and reads none. A Markdown file that a test reads
  would need its own exception.
- The two passes guard switches whose code is under `FEXCore/Source/Interface/Core`
  (`Dispatcher/` and `OpcodeDispatcher.cpp`). Their entries in `Config.json.in` and `Context.h`
  are outside it, so a pull request that changes only those runs without the two passes. The
  next push that owes its tests runs them.
- Any doubt runs everything: a lookup that fails, an empty list, a list at the compare's cap of
  300 files (checked against a 1,007-file diff, which it cut to 300), or a failed fetch of either
  version of `gw2.yml`. The step summary says which it decided and why.

## Alternatives I rejected

- **The selection with `git diff`, as my Wine fork's ADR 0015 does.** The core job checks out
  one commit without history, and fetching the history would add to every run. The compare API gives the
  same list in one call, before either job checks anything out.
- **A job of its own for the selection.** Both jobs would wait for it, and the step they share
  already calls GitHub's API.
- **Dropping the two passes altogether.** They are cheap on a push, and the dispatcher's L1
  lookup is a default since ADR 0008. A change to its code still meets them on its pull request.

## Evidence

- jdrly/gw2-on-mac #326.
- Run 44102293640 (pull request #20, Markdown only) for what a docs-only pull request paid. Push
  run 44435913533 for the same path without the tests.
- GW2 on Mac workspace: `advisor-plans/2026-11-19-ci-audit/AUDIT.md`, recommendation 12.
- The step's script, run on this server against pull requests #18 to #23, the pushes of
  `9b0d320f5` and `8a969bc74`, commit `502a1900e` (the dispatcher alone), a 1,007-file diff and a
  repository that doesn't exist. #20 came out docs only, #23 with two passes fewer, every failed
  lookup as "every test".
