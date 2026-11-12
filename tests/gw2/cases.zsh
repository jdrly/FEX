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

# My FEX's counters (jdrly/gw2-on-mac#117), only under mine: the engine's FEX has no such fields,
# and on macOS it gives no slot to a thread after its first page's worth. counters.exe keeps 200
# threads alive while it is read, exits into native code 100,000 times, outgrows the first code
# buffer and calls its code until the dynamic L1 has grown (tests/gw2/counters.c). Its main
# thread is the only one that does any of it. With FEX_DYNAMICL1CACHE=0 the L1 never resizes.
counters='gw2 test counters: passed'
counted='thread <->: * native_exits=<100000-> code_buffer_moves=<1-> l1_resizes='
stats counters mine 300 counters.exe "$counters" "$counted<1->"
stats counters-l1-fixed mine 300 counters.exe "$counters" "${counted}0" FEX_DYNAMICL1CACHE=0
if [[ $which == mine ]]; then
  reads=($prefix/counters.reader.<->(Nn))
  lines=()
  (( ${#reads} )) && lines=(${(f)"$(<$reads[-1])"})
  # every thread has a slot: the main thread and the 200 others, and Wine's own if it started any
  slots=${#${(M)lines:#thread <->: *}}
  if (( slots >= 201 )); then
    print "ok    counters-slots: $slots threads with a slot"
  else
    print "FAIL  counters-slots: $slots threads with a slot, not 201 or more"
    failed+=1
  fi
  # two switches for each Sleep(0) where FEX switches hardware TSO per thread, none without it
  if [[ $(<$prefix/hardware-tso) == yes ]]; then tso='<2000->'; else tso=0; fi
  if (( ${lines[(I)thread <->: * tso_switches=$~tso native_exits=<100000-> *]} )); then
    print "ok    counters-tso: the main thread's tso_switches are $tso"
  else
    print "FAIL  counters-tso: the main thread's tso_switches aren't $tso"
    failed+=1
  fi
fi
