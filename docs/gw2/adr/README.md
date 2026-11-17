# ADRs of my FEX fork

The design decisions of this fork, one per file. They sit under `docs/gw2/` so they stay apart
from FEX's own `docs/`.

The order is by number, not by date. 0006 and 0007 were decided on 2026-11-17 and landed first.
0001 to 0004 record older decisions, from 2026-11-12 and 2026-11-13, and 0005 the project's rules.
Those five landed after 0006 and 0007, in the numbers kept free below them. A new ADR takes the
next free number.

| File | Decided | One line |
|---|---|---|
| [`0001-a-fork-of-my-own-with-ai-patches-and-nothing-goes-upstream.md`](0001-a-fork-of-my-own-with-ai-patches-and-nothing-goes-upstream.md) | 2026-11-12 | The fork exists for the game thread's cycles. Its first commit replaces FEX's no-AI files with my rule, and nothing goes to FEX-Emu or athei. |
| [`0002-every-change-is-an-option-that-is-off-in-the-fork.md`](0002-every-change-is-an-option-that-is-off-in-the-fork.md) | 2026-11-12, narrowed 2026-11-15 | Each change is a `FEX_*` option, off by default. Dev builds turn them on, except one that costs frames for a symptom nobody saw. |
| [`0003-the-build-is-gated-against-the-engine-fex.md`](0003-the-build-is-gated-against-the-engine-fex.md) | 2026-11-12 | The PE DLLs match the engine's byte for byte but for link times. The `.so` files hold a structural gate on Xcode 27. |
| [`0004-while-github-blocks-the-fork-a-mac-is-its-ci.md`](0004-while-github-blocks-the-fork-a-mac-is-its-ci.md) | 2026-11-13 | When GitHub blocks the workflows, a Mac builds and tests, the tree lands with `[skip ci]`, and the asset is uploaded by hand. |
| [`0005-the-project-rules-that-bind-this-fork.md`](0005-the-project-rules-that-bind-this-fork.md) | recorded 2026-11-17 | The project-wide rules in a sentence each, pointing to the workspace ADRs. |
| [`0006-a-hung-test-costs-a-minute-and-no-job-runs-past-45-minutes.md`](0006-a-hung-test-costs-a-minute-and-no-job-runs-past-45-minutes.md) | 2026-11-17 | A hung test is cut off after a minute, and no job runs past 45 minutes. |
| [`0007-the-test-stage-runs-in-two-lanes-inside-its-one-macos-job.md`](0007-the-test-stage-runs-in-two-lanes-inside-its-one-macos-job.md) | 2026-11-17 | The cases of `counters.exe` and `codebuf.exe` run in a lane of their own, beside the others, in the one macOS job. |
