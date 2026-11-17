# While GitHub blocks the fork's workflows, a Mac is its CI

Decided 2026-11-13. Status: accepted as the procedure for a blocked fork. Used once.

On 2026-11-13 GitHub cancelled a run of this fork mid-test with "repository invocation blocked".
Every push was stopped until I unlocked the repository by hand. Held behind it was the fix for a
crash that reached the game: with Debug logging on, my `ntdll.so` and my FEX together read a stats
slot whose page had just been remapped (jdrly/gw2-on-mac #139). At 02:29 I ordered it fixed that
night, with a rented Mac as the CI instead of GitHub. That worked, and it is how a blocked fork is
handled from now on.

## Consequences

- One implementer builds the base and the change on the Mac and gates the base against the engine's
  files. FEX's two PE DLLs came out byte for byte the engine's but for the link times.
- The fork's test stage runs there, with a count where a crash is the question. The cause was proven
  by 80 alternating runs per build: 7 crashes on the old head, 0 with the fix.
- The dispatcher lands the reviewer-approved tree as a squash commit with `[skip ci]` in its
  message, so no run starts. No other push goes to a blocked fork.
- The implementer builds that commit, packs the archive as the workflow's Publish step does and
  uploads it to `arm64-builds` by hand, labelled as built on the rented Mac.
- Before the first push after an unlock, the stage is trimmed: fixed-time busy loops become counts
  or seconds.
- FEX's own core tests can't run on macOS, so they are not part of this path. They had passed on
  GitHub for the fix's head before the block.
- The Mac was a rented Scaleway M1, shared with my SWTOR project, until about 2026-11-19. There is
  no standing machine for this. My rule for it from 2026-11-12 stands: one user, one folder, no
  runner services. That rule had kept my Wine and FEX builds off it, because their byte gate needs
  Mach9's paths under `/Users/runner`. On 2026-11-13 I changed that for this fork while it is
  blocked. FEX builds under the one user, and its gate is against the engine's PE files, which don't
  depend on the path. Wine stays on GitHub's runners.

## Alternatives I rejected

- **Waiting for the unlock.** My order was to fix it that night. The dev build I meant to play in
  the morning waited for the fix.
- **Pushing to see whether the block had lifted.** A run cancelled with that annotation means stop
  every push and tell me. Only I can unlock it.
- **A runner service or a runner user on the rented Mac.** I had ruled both out when the Mac was
  rented, and the order of 2026-11-13 didn't change that.

## Evidence

- GW2 on Mac workspace `plans/night/2026-11-11-plan.md`, 2026-11-13 01:20 (the block), 02:35 (my
  order of 02:29), 03:10 (the cause proven), 03:45 (the two `[skip ci]` commits, `b9e412fda` and
  `c2e25acce`) and 04:05 (the upload); `work/arm64-0.2/rented-mac-notes.md`.
- The dispatch project file `gw2-on-mac.md`, "A blocked fork".
- jdrly/gw2-on-mac #139 and #132, jdrly/FEX PR #6 (closed with a note).
