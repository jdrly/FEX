# A FEX fork of my own, with AI-written patches, and nothing goes upstream

Decided 2026-11-12. Status: accepted.

On the arm64 engine FEX translates all of the game's x86_64 code, and the game thread limits the
frame in busy places. My performance council of 2026-11-12 found its largest expected gains inside
FEX: a code buffer that gets thrown away at every growth, and two Mach traps around every system
call for TSO. Both need changes to FEX's code. So I made this fork, public, from athei/FEX at
`9d4fa4b46b1`, the commit Mach9 0.3.1 was built from. FEX's own `AGENTS.md` and `CLAUDE.md` said AI
must not be used to generate code for contributions to their project. That is FEX's rule for what it
accepts, and this fork contributes nothing. Its first commit replaces both files with my rule: AI
writes the patches here, and nothing ever goes to FEX-Emu/FEX or athei/FEX.

## Consequences

- No pull request, issue, comment or discussion upstream, not even a finding. If something here
  looks worth telling them, it comes to me and stops there.
- `gw2-on-mac` is the default branch and the only one with my commits. `macos-patched` and `main`
  mirror athei/FEX.
- Issues are off. The work is tracked in jdrly/gw2-on-mac with the label `area:fex`.
- I never make an annotated tag here, and no tag of FEX-Emu/FEX is fetched into the fork. FEX puts
  `git describe` into every built file, and it reads annotated tags only, so either would change the
  build. My own tags are lightweight and name an engine's commit (`engine-0.3.1`, `engine-0.4.0`).
- The fork moves to a new athei commit only when my app moves to the Mach9 engine built from it.
  2026-11-17: the base is `597e7ccbedd` (`engine-0.4.0`), the commit Mach9 0.4.0 was built from. My
  commits were rebased onto it, after a merge that keeps the old line in the history
  (jdrly/gw2-on-mac#260).
- FEX's own workflows are gone. They name self-hosted runners I don't have. `gw2.yml` is the only
  workflow.
- Every commit an agent wrote says so, and an agent that didn't write the change reviews it.

## Alternatives I rejected

- **Following FEX's `AGENTS.md` and `CLAUDE.md` as they stood.** The question was put to me with the
  council's FEX tickets, and my answer was to replace both in the fork's first commit. Their rule is
  about contributions, and this fork makes none.
- **Reporting the council's findings upstream.** The fork is treated as my DXMT fork is, where I'd
  have to rewrite a finding by hand before upstream could use it.

## Evidence

- This fork's `AGENTS.md` ("Nothing goes upstream", "Branches and tags").
- GW2 on Mac workspace: `plans/night/2026-11-11-plan.md`, 2026-11-12 19:10 (my approval of the FEX
  findings) and 19:35 (the fork created, my two answers);
  `council/2026-11-12-perf-overview/findings-fex.md`; `work/arm64-0.2/fex-tickets.md`.
- The dispatcher's note `fex-fork-policy` and the dispatch project file `gw2-on-mac.md`.
- jdrly/gw2-on-mac #109 (the fork, its build and baseline), jdrly/FEX PR #1 (`bff3f7c3686`).
