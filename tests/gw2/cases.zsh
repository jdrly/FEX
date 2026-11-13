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
version fex-version both 120 wait.exe 'gw2 test wait: passed'

# My FEX's counters (jdrly/gw2-on-mac#117), only under mine: the engine's FEX has no such fields,
# and on macOS it gives no slot to a thread after its first page's worth. counters.exe
# (tests/gw2/counters.c) starts 200 threads (read 1), exits into native code 101,000 times with
# 1,000 system calls among them (read 2), then outgrows the first code buffer and calls its code
# until the dynamic L1 has grown (read 3). Only its main thread does any of it, so its counts
# between two reads are that phase's, plus what the waits for the reader cost: a few dozen calls a
# second. With FEX_DYNAMICL1CACHE=0 the L1 never resizes.
counters='gw2 test counters: passed'
counted='thread <->: * code_buffer_moves=<1-> l1_resizes='

# counter <read> <thread id> <field>: the field's value in that thread's line of that read of the
# case, or -1 when there is none
counter() {
  local f=$prefix/$1 line=
  [[ -f $f ]] && line=${(M)${(f)"$(<$f)"}:#thread $2: *}
  if [[ $line == (#b)*\ $3=(<->)(|\ *) ]]; then print -- $match[1]; else print -- -1; fi
}

# delta <label> <from read> <to read> <thread id> <field>: how much the field grew between the two
# reads, or -1 when one has no such line
delta() {
  local a=$(counter "$1.reader.$2" "$4" "$5") b=$(counter "$1.reader.$3" "$4" "$5")
  if (( a < 0 || b < 0 )); then print -- -1; else print -- $(( b - a )); fi
}

# verdict <label> <ok or not> <what>
verdict() {
  if [[ $2 == ok ]]; then
    print "ok    $1: $3"
  else
    print "FAIL  $1: $3"
    failed+=1
  fi
}

# counters_check <label> <l1 resizes>: what the reads of that case have to show beyond its
# pattern, <l1 resizes> being a zsh pattern for the main thread's L1 resizes in phase 3
counters_check() {
  local label=$1 l1=$2 main tid r exits traps moves resizes log=()
  [[ -f $prefix/$label.log ]] && log=(${(f)"$(tr -d '\r' <"$prefix/$label.log")"})
  main=${${(M)log:#gw2 test counters: tid *}##* }
  local workers=(${=${(M)log:#gw2 test counters: workers *}#gw2 test counters: workers})
  integer missing=0
  # every one of the 200 has a slot
  for tid in $workers; do
    (( $(counter "$label.reader.1" "$tid" tso_switches) >= 0 )) || missing+=1
  done
  (( ${#workers} == 200 && missing == 0 )) && r=ok || r=no
  verdict $label-slots $r "${#workers} threads named, $missing of them without a slot"
  # phase 2 on the main thread: 101,000 exits; two switches for each Sleep(0) where FEX switches
  # hardware TSO per thread, none without it
  exits=$(delta $label 1 2 "$main" native_exits)
  traps=$(delta $label 1 2 "$main" tso_switches)
  (( exits >= 101000 && exits <= 106000 )) && r=ok || r=no
  verdict $label-exits $r "$exits exits into native code in phase 2, wanted 101,000 to 106,000"
  if [[ $(<$prefix/hardware-tso) == yes ]]; then
    (( traps >= 2000 && traps <= 2600 )) && r=ok || r=no
    verdict $label-tso $r "$traps hardware TSO switches in phase 2, wanted 2,000 to 2,600"
  else
    (( traps == 0 )) && r=ok || r=no
    verdict $label-tso $r "$traps hardware TSO switches in phase 2, wanted 0 without per-thread hardware TSO"
  fi
  # phase 3: at least one move to a new code buffer, and the L1 resizes
  moves=$(delta $label 2 3 "$main" code_buffer_moves)
  resizes=$(delta $label 2 3 "$main" l1_resizes)
  (( moves >= 1 )) && r=ok || r=no
  verdict $label-moves $r "$moves code buffer moves in phase 3, wanted 1 or more"
  [[ $resizes == $~l1 ]] && r=ok || r=no
  verdict $label-l1 $r "$resizes L1 resizes in phase 3, wanted $l1"
}

stats counters mine 300 counters.exe "$counters" "$counted<1->"
if [[ $which == mine ]]; then counters_check counters '<1->'; fi
stats counters-l1-fixed mine 300 counters.exe "$counters" "${counted}0" FEX_DYNAMICL1CACHE=0
if [[ $which == mine ]]; then counters_check counters-l1-fixed 0; fi

# The first code buffer's size, FEX_INITIALCODEBUFFERMB (jdrly/gw2-on-mac#112). codebuf.exe
# compiles more code than 16 MB holds and runs it twice, with a read of FEX's stats after each
# pass; codebuf_check.py judges the two reads. Unset, pass 2 compiles at least 20% of it again.
# At 128 one buffer holds both passes. An invalid value logs one error and acts as unset.
# FEX_SILENTLOG=0 shows that 128 logs no error and 100 logs one (__wine_dbg_output doesn't go
# through WINEDEBUG's channels). All of it runs under mine only: the engine's FEX doesn't know the
# variable, and the cases read the stats slots, which only my FEX's layout is sure to match.
codebuf() {
  local label=$1 mode=$2 engines=$3
  shift 3
  applies $engines || return 0
  stats $label $engines 600 codebuf.exe 'codebuf: passed' 'thread <->: *jit_count=<->*' "$@"
  python3 -I "$here/codebuf_check.py" $mode "$prefix/$label.log" "$prefix/$label.reader.1" "$prefix/$label.reader.2" ||
    failed+=1
}
codebuf codebuf-unset discard mine
codebuf codebuf-128 keep mine FEX_INITIALCODEBUFFERMB=128 FEX_SILENTLOG=0
codebuf codebuf-invalid invalid mine FEX_INITIALCODEBUFFERMB=100 FEX_SILENTLOG=0

# The stats object grows while threads count in their slots (jdrly/gw2-on-mac#139). slots.exe
# (tests/gw2/slots.c) keeps 8 threads counting in their slots while it starts 600 more, so the
# object grows five times under them. On macOS my FEX maps the whole object once and a growth maps
# nothing; before, each growth mapped it again over the live slots. One run: a pass can't prove
# the race gone, so more runs add cost, not proof. It has to pass, give every one of the 608
# threads a slot, and show native exits in each counting thread's. Under mine only, like the
# counters cases.
slots_check() {
  local label=$1 tid r log=()
  integer missing=0 quiet=0
  [[ -f $prefix/$label.log ]] && log=(${(f)"$(tr -d '\r' <"$prefix/$label.log")"})
  local counting=(${=${(M)log:#gw2 test slots: counting *}#gw2 test slots: counting})
  local waiting=(${=${(M)log:#gw2 test slots: waiting *}#gw2 test slots: waiting})
  for tid in $counting $waiting; do
    (( $(counter "$label.reader.1" "$tid" tso_switches) >= 0 )) || missing+=1
  done
  for tid in $counting; do
    (( $(counter "$label.reader.1" "$tid" native_exits) > 0 )) || quiet+=1
  done
  (( ${#counting} == 8 && ${#waiting} == 600 && missing == 0 && quiet == 0 )) && r=ok || r=no
  verdict $label-slots $r "${#counting} counting and ${#waiting} waiting threads named, $missing without a slot, $quiet counting threads with no exits counted"
}
stats slots mine 120 slots.exe 'gw2 test slots: passed' 'thread <->: *'
if [[ $which == mine ]]; then slots_check slots; fi

# Mach9's smoke test in both modes with the option at 16 and at 128
for mb in 16 128; do
  check smoke-default-$mb mine 40 fex.exe "$smoke" FEX_INITIALCODEBUFFERMB=$mb
  check smoke-smcchecks-full-$mb mine 160 fex.exe "$smoke" FEX_SMCCHECKS=full FEX_INITIALCODEBUFFERMB=$mb
done

# The watchdog's samples (jdrly/gw2-on-mac#132). hang.exe prints one line and sleeps for good, so
# run.zsh has to fail it at its 20-second limit and leave <label>.hang.<pid>.log for each Wine
# process of the run. It passes only when hang.exe got to its sleep and the sample of hang.exe itself
# (the case's program, or a command naming hang.exe) has frames: "(in <image>)" from sample,
# "frame #" from lldb. The lines under it name each sample and which tool gave it.
hang_check() {
  local out f head rc=0 files tools=() program=no slept=no
  integer stacks=0
  out=$("$here/run.zsh" "$wine" "$prefix" hang 20 'gw2 test hang: never' "$tests/hang.exe" 2>&1) || rc=$?
  [[ -f $prefix/hang.log ]] && tr -d '\r' <"$prefix/hang.log" | grep -xF 'gw2 test hang: sleeping' >/dev/null && slept=yes
  files=($prefix/hang.hang.<->.log(Nn))
  for f in $files; do
    head=$(head -n 1 "$f")
    tools+=(${${(M)${(f)"$(<$f)"}:#tool: *}#tool: })
    print -r -- "      ${f:t}: $head"
    if grep -qE '\(in [^)]+\)|frame #[0-9]' "$f"; then
      stacks+=1
      [[ $head == *"(the case's program)"* || ${(L)head} == *hang.exe* ]] && program=yes
    fi
  done
  if (( rc == 1 )) && [[ $slept == yes && $program == yes && $out == *'(exit 137, the time limit)'* && $out == *'where it stood: '* ]]; then
    print "ok    hang: hang.exe's own sample has a stack; ${#files} samples, $stacks with frames, from ${(j:; :)${(@u)tools}}"
  else
    print "FAIL  hang: exit $rc, slept $slept, hang.exe's sample with frames $program, ${#files} samples, $stacks with frames"
    print -r -- "$out" | sed 's/^/      /'
    failed+=1
  fi
}
if [[ $which == mine ]]; then hang_check; fi

# jdrly/gw2-on-mac#113: FEX_HARDWARETSOSYSCALLS, switch (the default) and keep. The engine's FEX
# doesn't know the option, and its stats slots have no trap count, so these run under mine only.
# tso-keep says whether keep took effect: FEX logs it only when threads are switched one by one,
# so it has to agree with hardware-tso.
check smoke-tso-switch mine 40 fex.exe "$smoke" FEX_HARDWARETSOSYSCALLS=switch
check smoke-tso-keep mine 40 fex.exe "$smoke" FEX_HARDWARETSOSYSCALLS=keep
check smoke-tso-keep-smcchecks-full mine 160 fex.exe "$smoke" FEX_HARDWARETSOSYSCALLS=keep FEX_SMCCHECKS=full
check smoke-tso-keep-log mine 40 fex.exe "$smoke" FEX_HARDWARETSOSYSCALLS=keep FEX_SILENTLOG=0 WINEDEBUG=
if [[ $which == mine ]]; then report tso-keep smoke-tso-keep-log '*FEX: Keeping hardware TSO across syscalls*'; fi

# Four threads in a ring of fence-free queues for 10 seconds, a syscall on every pass. keep moved
# 150 million items in that time (905 million in 60 s in run 43331549134), switch 3.6 million.
check tsoqueue-switch mine 150 tsoqueue.exe 'gw2 test tsoqueue: passed' FEX_HARDWARETSOSYSCALLS=switch
check tsoqueue-keep mine 150 tsoqueue.exe 'gw2 test tsoqueue: passed' FEX_HARDWARETSOSYSCALLS=keep

# tsocalls_traps <label> <mode>: the trap count of each of tsocalls.exe's loop threads, per
# iteration, from <label>.log's thread ids and the last read of <label>.reader.<n>. With hardware
# TSO: under switch about two per iteration of close and qpc (1.9 to 2.1), under keep fewer than
# one per 1,000; the native loop fewer than one per 1,000 in both. Without it every count is 0.
# tso_field is fexstats's name for the count (#117), one per trap. 100,000 iterations a loop: the
# close loop costs 40 to 70 us an iteration, so a million took 40 s and 70 s in run 43331549134,
# and a tenth still tells two traps an iteration from fewer than 100 in all.
tso_field=tso_switches tsocalls_iterations=100000
tsocalls_traps() {
  [[ $which == mine ]] || return 0
  local label=$1 mode=$2 hw=no keep=no loop line tid traps want ok=1 said=()
  [[ -f $prefix/hardware-tso ]] && hw=$(<$prefix/hardware-tso)
  [[ -f $prefix/tso-keep ]] && keep=$(<$prefix/tso-keep)
  local reads=($prefix/$label.reader.<->(Nn))
  local log=(${(f)"$(tr -d '\r' <"$prefix/$label.log")"})
  local last=(${(f)"$( (( ${#reads} )) && cat "$reads[-1]")"})
  if [[ $keep != "$hw" ]]; then
    print "FAIL  $label-traps: keep took effect: $keep, hardware TSO: $hw"
    failed+=1
    return 0
  fi
  integer n=$tsocalls_iterations
  for loop in native close qpc; do
    line=${log[(r)gw2 test tsocalls: $loop thread *]}
    tid=${${line#*thread }%% *}
    line=${last[(r)thread $tid: *]}
    traps=
    [[ $line == *" $tso_field="<->(|" "*) ]] && traps=${${line#* $tso_field=}%% *}
    if [[ -z $tid || -z $traps ]]; then
      print "FAIL  $label-traps: no $tso_field for the $loop loop's thread ${tid:-?}"
      failed+=1
      return 0
    fi
    said+=("$loop $traps")
    if [[ $hw != yes ]]; then
      (( traps == 0 )) || ok=0
    elif [[ $mode == switch && $loop != native ]]; then
      (( 10 * traps >= 19 * n && 10 * traps <= 21 * n )) || ok=0
    else
      (( 1000 * traps < n )) || ok=0
    fi
  done
  if (( ok )); then
    print "ok    $label-traps: $n iterations, hardware TSO $hw, traps: ${(j:, :)said}"
  else
    print "FAIL  $label-traps: $n iterations, hardware TSO $hw, traps: ${(j:, :)said}"
    failed+=1
  fi
}
stats tsocalls-switch mine 300 tsocalls.exe 'gw2 test tsocalls: passed' 'thread <->: *' \
  FEX_HARDWARETSOSYSCALLS=switch GW2_TSOCALLS_ITERATIONS=$tsocalls_iterations
tsocalls_traps tsocalls-switch switch
stats tsocalls-keep mine 300 tsocalls.exe 'gw2 test tsocalls: passed' 'thread <->: *' \
  FEX_HARDWARETSOSYSCALLS=keep GW2_TSOCALLS_ITERATIONS=$tsocalls_iterations
tsocalls_traps tsocalls-keep keep

# jdrly/gw2-on-mac#161: FEX_DISPATCHERL1LOOKUP, the dispatcher's own look in L1. l1lookup.exe
# (tests/gw2/l1lookup.c) has qsort in ucrtbase.dll call a comparator in the program at least
# 100,000 times between its two reads, each call an entry from native code and its return an exit
# into native code. Off, each entry is a lookup in C++ that L1 answers, so l1_hits grows by about
# the calls (99% to 110%: a shrink of the dynamic L1 sends a few to L3). On, the dispatcher answers
# them, and l1_hits grows by fewer than 100. native_exits has to grow by at least the calls in both
# modes, or qsort didn't run as native code and the case proves nothing. Under mine only: the engine's
# FEX doesn't know the switch, and l1_hits is padding in its slots.
l1lookup_check() {
  local label=$1 mode=$2 main calls hits exits r log=()
  [[ -f $prefix/$label.log ]] && log=(${(f)"$(tr -d '\r' <"$prefix/$label.log")"})
  main=${${(M)log:#gw2 test l1lookup: tid *}##* }
  calls=${${(M)log:#gw2 test l1lookup: calls *}##* }
  [[ $calls == <-> ]] || calls=0
  hits=$(delta $label 1 2 "$main" l1_hits)
  exits=$(delta $label 1 2 "$main" native_exits)
  (( calls >= 100000 && exits >= calls )) && r=ok || r=no
  verdict $label-native $r "$calls comparator calls, $exits exits into native code, wanted at least 100,000 and at least the calls"
  if [[ $mode == off ]]; then
    (( hits >= 0 && 100 * hits >= 99 * calls && 10 * hits <= 11 * calls )) && r=ok || r=no
    verdict $label-hits $r "$hits L1 hits in C++ for $calls calls, wanted 99% to 110% of them"
  else
    (( hits >= 0 && hits < 100 )) && r=ok || r=no
    verdict $label-hits $r "$hits L1 hits in C++ for $calls calls, wanted fewer than 100"
  fi
}
stats l1lookup-off mine 120 l1lookup.exe 'gw2 test l1lookup: passed' 'thread <->: * l1_hits=<->*' FEX_DISPATCHERL1LOOKUP=0
if [[ $which == mine ]]; then l1lookup_check l1lookup-off off; fi
stats l1lookup-on mine 120 l1lookup.exe 'gw2 test l1lookup: passed' 'thread <->: * l1_hits=<->*' FEX_DISPATCHERL1LOOKUP=1
if [[ $which == mine ]]; then l1lookup_check l1lookup-on on; fi

# Single steps and int3 into code already in L1 (tests/gw2/singlestep.c): each resume goes through
# the dispatcher, whose trap flag check has to come before its L1 lookup. Under both FEXes with
# the switch unset, and under mine with it on. Then Mach9's smoke test with it on, with and without
# FEX_SMCCHECKS=full, whose invalidations clear L1 entries that the dispatcher now reads.
check singlestep both 60 singlestep.exe 'gw2 test singlestep: passed'
check singlestep-l1lookup mine 60 singlestep.exe 'gw2 test singlestep: passed' FEX_DISPATCHERL1LOOKUP=1
check smoke-l1lookup mine 40 fex.exe "$smoke" FEX_DISPATCHERL1LOOKUP=1
check smoke-l1lookup-smcchecks-full mine 160 fex.exe "$smoke" FEX_DISPATCHERL1LOOKUP=1 FEX_SMCCHECKS=full

# A crash shows as one (jdrly/gw2-on-mac#132). crash.exe prints one line and writes to address 16.
# With the prefix's crash dialog off (run.zsh), winedbg --auto prints the exception and the
# backtrace into the log and the program ends with an exit of its own, within seconds. With the
# dialog on, winedbg would sit in it until the 60-second limit (exit 137), and the log would have
# no backtrace. Under both FEXes: each prefix gets the setting when run.zsh boots it.
# crash_check [<label> <VAR=value>...]: the case under that label, crash by default, with those
# variables
crash_check() {
  local label=${1:-crash} out rc=0 crashed=no backtrace=no
  (( $# )) && shift
  integer start=$SECONDS took
  out=$("$here/run.zsh" "$wine" "$prefix" $label 60 'gw2 test crash: never' "$tests/crash.exe" "$@" 2>&1) || rc=$?
  took=$(( SECONDS - start ))
  local log=()
  [[ -f $prefix/$label.log ]] && log=(${(f)"$(tr -d '\r' <"$prefix/$label.log")"})
  (( ${log[(I)gw2 test crash: crashing]} )) && crashed=yes
  (( ${log[(I)Backtrace:]} )) && backtrace=yes
  if (( rc == 1 )) && [[ $crashed == yes && $backtrace == yes && $out == *', a crash: wine: Unhandled page fault '* &&
    $out != *'(exit 137'* && $out != *'processes left behind'* ]]; then
    # outside quotes, where (f) gives lines and (M) keeps the one
    local line=(${(M)${(f)out}:#FAIL  $label *})
    print -r -- "ok    $label: ${line[1]#FAIL  $label }, with winedbg's backtrace in $label.log"
  else
    print "FAIL  $label: exit $rc after $took s, got to the crash $crashed, backtrace $backtrace"
    print -r -- "$out" | sed 's/^/      /'
    failed+=1
  fi
}
crash_check

# jdrly/gw2-on-mac#162: FEX_ENTRYHEADERLOOKUP=1, under which no block entry stores its header and
# FEX finds the block of a fault, suspend or SMC write from the host PC. entryfault.exe
# (tests/gw2/entryfault.c) faults at a function's first block, after a call returns, three calls
# deep and in a hot loop, writes into the block it runs in, and suspends a thread spinning in
# translated code 1,000 times. Under both FEXes without the option, which checks the program,
# and under mine with it, also with FEX_SMCCHECKS=full. Then, under mine with it on: the crash,
# Mach9's smoke test in both modes, tsoqueue, tsocalls, codebuf (unset, so the code buffer moves,
# and at 128) and counters.
lookup=FEX_ENTRYHEADERLOOKUP=1
entryfault='gw2 test entryfault: passed'
check entryfault both 120 entryfault.exe "$entryfault"
check entryfault-lookup mine 120 entryfault.exe "$entryfault" $lookup
check entryfault-lookup-smcchecks-full mine 160 entryfault.exe "$entryfault" $lookup FEX_SMCCHECKS=full
if [[ $which == mine ]]; then crash_check crash-lookup $lookup; fi
check smoke-default-lookup mine 40 fex.exe "$smoke" $lookup
check smoke-smcchecks-full-lookup mine 160 fex.exe "$smoke" $lookup FEX_SMCCHECKS=full
check tsoqueue-keep-lookup mine 150 tsoqueue.exe 'gw2 test tsoqueue: passed' FEX_HARDWARETSOSYSCALLS=keep $lookup
stats tsocalls-keep-lookup mine 300 tsocalls.exe 'gw2 test tsocalls: passed' 'thread <->: *' \
  FEX_HARDWARETSOSYSCALLS=keep GW2_TSOCALLS_ITERATIONS=$tsocalls_iterations $lookup
tsocalls_traps tsocalls-keep-lookup keep
codebuf codebuf-unset-lookup discard mine $lookup
codebuf codebuf-128-lookup keep mine FEX_INITIALCODEBUFFERMB=128 FEX_SILENTLOG=0 $lookup
stats counters-lookup mine 300 counters.exe "$counters" "$counted<1->" $lookup
if [[ $which == mine ]]; then counters_check counters-lookup '<1->'; fi
