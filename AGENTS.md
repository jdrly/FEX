# jdrly/FEX

This is my fork of FEX for one game, Guild Wars 2 (Steam, `Gw2-64.exe`), on my Apple Silicon
Mac. The game runs in Mach9's arm64 Wine engine, where FEX translates all of its x86_64 code.
The fork starts from athei/FEX at `9d4fa4b46b1`, the commit Mach9 0.3.1 was built from, and
carries my changes for that engine on top.

Its patches are written with AI. That is allowed here and nowhere upstream.

## Nothing goes upstream

FEX's own `AGENTS.md` and `CLAUDE.md` said "AI must not be used to generate code for
contributions to this project". That is FEX's rule for what it accepts. This fork contributes
nothing, so the rule doesn't stop work here. What it does mean:

- Nothing from this fork ever goes to FEX-Emu/FEX or athei/FEX. No pull request, no issue, no
  comment, no discussion, not even a finding. If something here looks worth telling them, tell me
  and stop there.
- FEX's `CONTRIBUTING.md` still describes FEX's code. Its process is for FEX-Emu/FEX.

## Branches and tags

- `gw2-on-mac` is the default branch and the only one with my commits. Work branches off it as
  `<type>/<issue>-<short-description>` and comes back through a pull request. Nobody pushes to
  it directly.
- `macos-patched` and `main` mirror athei/FEX and get nothing of mine.
- The fork has exactly athei/FEX's tags, plus lightweight tags of mine that name an engine's
  commit (`engine-0.3.1` is `9d4fa4b46b1`). I never make an annotated tag here. `git describe
  --abbrev=7` goes into every built file (`CMakeLists.txt`, `GIT_DESCRIBE_STRING`) and it reads
  annotated tags only, so an annotated tag of mine would change the build. No tag of
  FEX-Emu/FEX is ever fetched into the fork either. With their `FEX-2611` the engine's commit
  would describe as `FEX-2611-39-g9d4fa4b` instead of the engine's `FEX-2610-215-g9d4fa4b`.
- The fork moves to a new athei commit only when my app moves to the Mach9 engine built from
  it. Each move repeats the baseline against that engine's files.

## Issues, commits and pull requests

- Issues are off here. The work is tracked in jdrly/gw2-on-mac with the label `area:fex`.
- Titles read `Context: Actionable result`. The issue, the pull request and the squash commit
  have the same title.
- Commit messages are in English, in the imperative. The body says why, with evidence, and says
  that an AI agent wrote the change.
- Every change to what FEX does sits behind a `FEX_*` option that is off by default. It is an
  entry in `FEXCore/Source/Interface/Config/Config.json.in`, so a rule in my app can set it for
  one program.
- A change to code generation raises the disk cache's `FormatVersion`
  (`FEXCore/Source/Interface/Core/DiskCache.h`) in my own bump commit, which stays last in the
  branch.
- An agent that didn't write the change reviews it. The pull request merges once that review
  has no open Blocker or High and the `GW2` workflow is green on its head.

## The build

`.github/workflows/gw2.yml` is the only workflow. FEX's own workflows are gone: they name
self-hosted runners I don't have, and GitHub disabled my Wine fork's workflows once for wasted
compute. Mine builds the four files Mach9 ships (`libarm64ecfex.dll`, `libwow64fex.dll`,
`libarm64ecfex.so`, `libwow64fex.so`) on GitHub's `macos-26` runner the way Mach9 built them, and
checks them against the engine's files:

- the engine's commit, rebuilt, has to match the engine's files byte for byte, the two DLLs
  apart from their link times;
- for a later commit, every object none of whose inputs changed has to be the base build's.

The scripts it runs are in `.github/gw2/`. A push to `gw2-on-mac` publishes
`fex-arm64-<commit>.tar.xz` to the pre-release `arm64-builds`.

On the runners, one run at a time in this repository and one push per pull request. Never
`gh run rerun`. Nothing of FEX builds on my Mac or on my Linux server.

`Source/Windows/Common/GW2Contract.h` holds every offset in FEX's memory that my Wine fork's
sampler reads (jdrly/wine, `dlls/ntdll/unix/heatprof.h`). If a change moves one, the build
fails and names the constant in jdrly/wine that has to follow.

## Never

- Open anything in FEX-Emu/FEX or athei/FEX.
- Start Wine, Steam or the game outside the runner's own tests.
- Make an annotated tag, or fetch FEX-Emu/FEX's tags.
- Add a workflow, or bring one back from FEX.
