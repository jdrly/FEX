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

# Four threads in a ring of fence-free queues for 60 seconds, a syscall on every pass
check tsoqueue-switch mine 150 tsoqueue.exe 'gw2 test tsoqueue: passed' FEX_HARDWARETSOSYSCALLS=switch
check tsoqueue-keep mine 150 tsoqueue.exe 'gw2 test tsoqueue: passed' FEX_HARDWARETSOSYSCALLS=keep

# tsocalls_traps <label> <mode>: the trap count of each of tsocalls.exe's loop threads, per
# iteration, from <label>.log's thread ids and the last read of <label>.reader.<n>. With hardware
# TSO: under switch about two per iteration of close and qpc (1.9 to 2.1), under keep fewer than
# one per 1,000; the native loop fewer than one per 1,000 in both. Without it every count is 0.
# tso_field is fexstats's name for the count (#117), one per trap.
tso_field=tso_switches tsocalls_iterations=1000000
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
