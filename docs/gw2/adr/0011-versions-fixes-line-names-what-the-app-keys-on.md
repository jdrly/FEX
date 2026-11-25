# VERSION's `fixes:` line names the fixes of mine the app keys on

Decided 2026-11-26. Status: accepted.

On 2026-11-20 the game crashed at start on my M5 Max, twice in a row, with Debug logging on
(jdrly/gw2-on-mac#344). Debug logging puts `FEX_PROFILESTATS=1` in the game's rule whenever my
`ntdll.so` is in place, and that night the engine's own FEX was. The engine's FEX gives each thread a
stats slot in a shared memory object of one page. On macOS the object can't grow, because macOS sets a
shared memory object's size only once. FEX then takes the failed growth for a size of 0, and the 37th
thread reads past the page in `StatAllocBase::AllocateSlot` and faults. My fork fixed this on
2026-11-13 (#117, #139). On macOS, `InitializeSHM` sizes the object once to `MAX_STATS_SIZE` and a
growth maps nothing. On every platform, `StatAlloc::FrontendAllocateSlots` keeps the size it has when
a call fails. The 4 MB hold about 29,000 slots, and a thread past them gets none, without a fault.

The app can't tell that from the files. `switches:` names options, and this fix has none: it changes
what `FEX_PROFILESTATS` does, and the crash it removes is a macOS one. So VERSION gets a second
line the app reads, `fixes: <id> …`. Each id names one fix of mine that the app's rules depend on.
The first is `profilestats-slots`. The app puts `FEX_PROFILESTATS=1` in the game's rule only when my FEX is in
place and its VERSION names it.

## Consequences

- The Stage step of `gw2.yml` writes the line after `switches:`. An id goes in with the fix that
  earns it, and its comment there names the issue and the test that checks it. For
  `profilestats-slots` that is `slots.exe` in the test stage, 608 threads under `FEX_PROFILESTATS=1`.
- My builds since #117 and #139 have the fix but not the line, and the app treats them as builds
  without it. With one of them, Debug logging writes no FEX counters. Nothing crashes.
- An id stays on the line for as long as the fix is in the tree. Removing the fix removes the id in
  the same pull request.

## Alternatives I rejected

- **A `FEX_*` option for the fix (ADR 0002).** The broken path runs only with `FEX_PROFILESTATS=1`, and
  turning the fix off would bring the crash back. ADR 0002 is about changes to what FEX does to the
  game, not about a crash in a counter.
- **The app keying on "my FEX in place" alone.** It would hold for every build of mine on the
  `gw2-on-mac` branch that the app can install today. But a dev build can name any commit on that
  branch, including 0.3.1-era ones from before #117. An app can only check a record, not git
  ancestry.
- **A line that the workflow derives from the source.** A grep of `FEXUnixLib.cpp` would break on the
  next rewording and prove less than `slots.exe` does.

## Evidence

- jdrly/gw2-on-mac#344: the crash, its address in `libarm64ecfex.dll`, and a native reproduction on
  macOS 26.7.3 (the growth's `ftruncate` fails with EINVAL, then the scan faults past the mapping).
- jdrly/gw2-on-mac#117 and #139, jdrly/FEX PR #3 and #6: the fix and `tests/gw2/slots.c`.
