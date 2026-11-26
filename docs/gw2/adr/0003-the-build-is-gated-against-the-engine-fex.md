# The build is gated against the engine's FEX files

Decided 2026-11-12. Status: accepted.

My app replaces the engine's four FEX files (`libarm64ecfex.dll`, `libwow64fex.dll`,
`libarm64ecfex.so`, `libwow64fex.so`) with this fork's. As in my Wine fork, "the engine's file plus
my change" has to be shown. The workflow rebuilds the engine's commit the way Mach9 built it and
holds that baseline to the engine's files. For a later commit, every object none of whose inputs
changed has to be the baseline's, from the same run and toolchain.

## Consequences

- The two PE DLLs come from a pinned llvm-mingw and match the engine's byte for byte apart from
  their two link times.
- The two `.so` files are the engine's byte for byte only on the engine's runner and Xcode
  (`macos-26`, Xcode 26.6). Since 2026-11-12 the build runs on `xcode-27`, because `macos-26`
  runners weren't being assigned, so they get a structural gate: the engine's exports, imports,
  libraries, `minos`, segments, sections and functions by name. Which functions differ in their
  bytes is reported, not failed. Going back is one edit at the top of the workflow.
- The first run on `xcode-27` went red only on the linker's section order in `__DATA_CONST`, a
  requirement nobody had asked for. It was dropped.
- A broken build must fail and name its function. The check named the one changed function of 5,850.
- `Source/Windows/Common/GW2Contract.h` holds every offset in FEX's memory that my Wine fork's
  sampler reads. A change that moves one fails the build and names the constant in jdrly/wine that
  has to follow.
- A push to `gw2-on-mac` publishes `fex-arm64-<commit>.tar.xz` to the pre-release `arm64-builds`,
  and dev builds fetch it by commit.
- FEX's own core tests can't run on macOS. They run on `ubuntu-24.04-arm`, where the same 35 fail at
  the engine's commit (18 asm of 7,689, 17 InstCountCI of 108). That list is the reference.
  2026-11-17, Mach9 0.4.0's `597e7ccbedd`: the same 18 asm (of 7,695) and 17 InstCountCI (of 110),
  and five x87 listings whose numbers athei's `ddf3ea74f` changed without updating them, 40 lines
  (jdrly/FEX run 44191738763, jdrly/gw2-on-mac#260).

## Alternatives I rejected

- **Staying on `macos-26` for the byte gate.** No runner came. My decision that day was to compile
  on Xcode 27, for this fork as for my Wine fork.
- **Holding the `.so` files to the linker's section order.** It failed the first run and nobody had
  asked for it.

## Evidence

- jdrly/gw2-on-mac #109, #124, jdrly/FEX PR #1 (`bff3f7c3686`) and PR #2 (`3fcc436a6df`).
- This fork's `AGENTS.md` ("The build").
- GW2 on Mac workspace `plans/night/2026-11-11-plan.md`, 2026-11-12 20:20 (my decision to compile on
  Xcode 27; #109 told the same), 20:50 and 21:50.
- My Wine fork's ADRs 0002 and 0003, which this one follows.
