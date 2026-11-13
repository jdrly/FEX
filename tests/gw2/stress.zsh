#!/bin/zsh
# gw2 (jdrly/gw2-on-mac#132): how often counters.exe and Mach9's smoke program crash or hang, under
# the engine's FEX and under mine, with FEX_DYNAMICL1CACHE unset and at 0. Mine runs twice, the
# second time with FEX_ENTRYHEADERLOOKUP=1 (jdrly/gw2-on-mac#162, "lookup" in the table and the
# work dir). The manual job of .github/workflows/gw2.yml runs it, on my word only.
#
#   stress.zsh <engine wine> <my wine> <tests dir> <work dir> <rounds> <seconds> <report>
#
# <engine wine>, <my wine> and <tests dir> as for tests/gw2/engine.zsh. Each round runs every
# combination once through tests/gw2/run.zsh, in a prefix for each FEX under <work dir>. In each
# round the smoke program comes first, as it takes seconds, and the combination that crashed in the
# stage (counters.exe, mine, FEX_DYNAMICL1CACHE=0) leads the rest, so a run cut short still has them:
#   - counters.exe under my FEX as the stage's counters cases run it (FEX_PROFILESTATS=1, a reader
#     at each of its waits, here fexstats --header). The stage's case crashed while the program
#     started its 200 threads, before the first read (jdrly/gw2-on-mac#139). The engine's FEX
#     can't run it with FEX_PROFILESTATS=1 on macOS: its stats object never grows past the first
#     27 slots, and with 200 threads it faults just past them and ends the program with exit 5
#     (on my rented Mac, 2026-11-13; #117 fixed that in mine). Under it counters.exe runs without
#     the stats, and the reader is /usr/bin/true, which only lets the program go on;
#   - fex.exe (Mach9's smoke/fex.c) as the stage's smoke-default runs it.
# The rounds stop after <rounds>, or once <seconds> have gone by since the start: no run starts
# after that, so the table may hold fewer runs of the later combinations.
#
# A run ended in one of four ways: passed; crashed (Wine said "wine: Unhandled …", run.zsh's "a
# crash"); hung (killed at its time limit, exit 137); failed otherwise. <report> gets a Markdown
# table of them, a line for each run that didn't pass, and each run's time, written again after
# every run: a job that stops this script at its own time limit still has the table so far. The
# logs are <work dir>/<engine, mine or lookup>/<label>.log, the samples of a hang beside them. Exits 0
# unless its arguments are wrong: a crash here is a finding, not a failure.
emulate -L zsh
setopt err_exit pipe_fail no_unset extended_glob

(( $# == 7 )) || { print -u2 "usage: stress.zsh <engine wine> <my wine> <tests dir> <work dir> <rounds> <seconds> <report>"; exit 2 }
[[ $5 == <1-> && $6 == <1-> ]] || { print -u2 "stress.zsh: <rounds> and <seconds> are whole numbers above 0"; exit 2 }
typeset -A wines=(engine ${1:A} mine ${2:A} lookup ${2:A}) names=(engine "the engine's" mine mine lookup "mine, FEX_ENTRYHEADERLOOKUP=1")
tests=${3:A} work=${4:A} out_file=${7:A}
integer rounds=$5 budget=$6 round start=$SECONDS t rc
here=${0:A:h}
smoke='wine-build smoke test: x86_64 FEX cases passed'

typeset -a combos notes
typeset -A runs passed crashed hung other secs
for program in smoke counters; do
  for l1 in 0 unset; do
    for which in mine lookup engine; do
      combos+=($program:$which:$l1)
    done
  done
done
for c in $combos; do runs[$c]=0 passed[$c]=0 crashed[$c]=0 hung[$c]=0 other[$c]=0 secs[$c]=0; done

# the report so far, into <report> through a file beside it, so a reader never sees half of it
report() {
  local c program which l1
  {
    print "### Crashes and hangs at process start\n"
    print "$(( SECONDS - start )) s so far, for at most $rounds rounds, no run started after $budget s.\n"
    print "| program | FEX | FEX_DYNAMICL1CACHE | runs | passed | crashed | hung | failed otherwise | seconds a run |"
    print "|---|---|---|---|---|---|---|---|---|"
    for c in $combos; do
      program=${c%%:*} which=${${c#*:}%%:*} l1=${c##*:}
      print "| $program | $names[$which] | $l1 | $runs[$c] | $passed[$c] | $crashed[$c] | $hung[$c] | $other[$c] |" \
        "$(( runs[$c] ? secs[$c] / runs[$c] : 0 )) |"
    done
    print
    if (( ${#notes} )); then
      print "Runs that didn't pass (logs and samples in the stress artifact, under engine/, mine/ and lookup/):\n"
      print -rl -- $notes
    else
      print "No run failed."
    fi
  } >"$out_file.new"
  mv -f "$out_file.new" "$out_file"
}
report

for (( round = 1; round <= rounds; round++ )); do
  for c in $combos; do
    (( SECONDS - start < budget )) || break 2
    program=${c%%:*} which=${${c#*:}%%:*} l1=${c##*:}
    label=$program-l1-$l1-$round
    vars=()
    [[ $l1 == unset ]] || vars=(FEX_DYNAMICL1CACHE=$l1)
    [[ $which != lookup ]] || vars+=(FEX_ENTRYHEADERLOOKUP=1)
    mkdir -p "$work/$which"
    rc=0 t=$SECONDS
    if [[ $program == counters && $which != engine ]]; then
      out=$(GW2_TEST_READER="$tests/fexstats --header" "$here/run.zsh" "$wines[$which]" "$work/$which" "$label" 120 \
        'gw2 test counters: passed' "$tests/counters.exe" $vars FEX_PROFILESTATS=1 2>&1) || rc=$?
    elif [[ $program == counters ]]; then
      out=$(GW2_TEST_READER=/usr/bin/true "$here/run.zsh" "$wines[$which]" "$work/$which" "$label" 120 \
        'gw2 test counters: passed' "$tests/counters.exe" $vars 2>&1) || rc=$?
    else
      out=$("$here/run.zsh" "$wines[$which]" "$work/$which" "$label" 40 "$smoke" "$tests/fex.exe" $vars 2>&1) || rc=$?
    fi
    t=$(( SECONDS - t ))
    runs[$c]=$(( runs[$c] + 1 )) secs[$c]=$(( secs[$c] + t ))
    fails=(${(M)${(f)out}:#FAIL  *})
    first=${fails[1]-}
    if (( rc == 0 )); then
      passed[$c]=$(( passed[$c] + 1 ))
    elif [[ $first == *', a crash: '* ]]; then
      crashed[$c]=$(( crashed[$c] + 1 ))
    elif [[ $first == *'(exit 137, the time limit'* ]]; then
      hung[$c]=$(( hung[$c] + 1 ))
    else
      other[$c]=$(( other[$c] + 1 ))
    fi
    (( rc == 0 )) || notes+=("- $which/$label, after $t s: ${first:-exit $rc}")
    report
  done
done
report

