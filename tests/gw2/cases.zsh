# gw2 (jdrly/gw2-on-mac#109): the test stage's cases, run under the engine's FEX and under mine by
# tests/gw2/engine.zsh, which says what check, stats and report do. A new test is a program
# (tests/gw2/<name>.c, built for Win64 x86_64 as <name>.exe) and a line here.

# Mach9's FEX smoke test (athei/mach9 smoke/fex.c), in FEX's default mode and with
# FEX_SMCCHECKS=full, as bin/bundle-wine.sh's run_fex runs it. The limits are Mach9's, 10 and
# 40 seconds, times four: the first JIT of a fresh process on a hosted runner.
smoke='wine-build smoke test: x86_64 FEX cases passed'
check smoke-default both 40 fex.exe "$smoke"
check smoke-smcchecks-full both 160 fex.exe "$smoke" FEX_SMCCHECKS=full

# The same with FEX's log on and Wine's default channels. FEX says "FEX: Using hardware TSO"
# through __wine_dbg_output when the trap works (Source/Windows/Common/HardwareTSO.cpp), so
# hardware-tso says whether this runner has it.
check smoke-log both 40 fex.exe "$smoke" FEX_SILENTLOG=0 WINEDEBUG=
report hardware-tso smoke-log '*FEX: Using hardware TSO*'

# Which FEX ran: its version in the header of its stats object
stats fex-version both 120 wait.exe 'gw2 test wait: passed' 'fex-<->-stats: *, FEX {version}'
