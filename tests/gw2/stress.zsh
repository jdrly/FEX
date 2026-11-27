#!/bin/zsh
# gw2 (jdrly/gw2-on-mac#132): how often counters.exe and Mach9's smoke program crash or hang, under
# the engine's FEX and under mine, with FEX_DYNAMICL1CACHE unset and at 0. Mine runs twice: as it
# is, which has FEX_ENTRYHEADERLOOKUP and FEX_DISPATCHERL1LOOKUP on (jdrly/gw2-on-mac#266), and
# with both of them at 0 ("off" in the table and the work dir). The manual job of
# .github/workflows/gw2.yml runs it, on my word only.
#
#   stress.zsh <engine wine> <my wine> <tests dir> <work dir> <rounds> <report>
#   stress.zsh --budget <rounds>
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
# The rounds stop after <rounds>, or once the budget has gone by since the start: no run starts
# after that. The budget is <rounds> times a round's planned seconds, the sum over the combinations
# of what a run of its program takes (per_run below, from the runner's times), plus the making of
# each FEX's prefix. --budget prints it, as seconds=<n>, and the minutes the step needs for it with
# one run started at its end and killed at its time limit, as minutes=<n>, for $GITHUB_OUTPUT: the
# job's limits follow from these, so a new combination moves them too. A run cut short by the
# budget, a hang or slow runs leaves combinations with fewer than <rounds> runs; the table names
# them.
#
# A run ended in one of four ways: passed; crashed (Wine said "wine: Unhandled …", run.zsh's "a
# crash"); hung (killed at its time limit, exit 137); failed otherwise. <report> gets a Markdown
# table of them, a line for each run that didn't pass, and each run's time, written again after
# every run: a job that stops this script at its own time limit still has the table so far. The
# logs are <work dir>/<engine, mine or off>/<label>.log, the samples of a hang beside them. Exits 0
# unless its arguments are wrong: a crash here is a finding, not a failure.
emulate -L zsh
setopt err_exit pipe_fail no_unset extended_glob

usage() {
  print -u2 "usage: stress.zsh <engine wine> <my wine> <tests dir> <work dir> <rounds> <report>\n       stress.zsh --budget <rounds>"
  exit 2
}
(( $# == 6 )) || [[ $# == 2 && $1 == --budget ]] || usage
[[ $1 == --budget ]] && rounds_arg=$2 || rounds_arg=$5
[[ $rounds_arg == <1-> ]] || { print -u2 "stress.zsh: <rounds> is a whole number above 0"; exit 2 }
integer rounds=$rounds_arg

# Seconds a run of each program takes on the runner (run 43471599150 on xcode-27, 2026-11-13: the
# smoke program 4 to 5 s, counters.exe 50 to 61 s), and the limit at which run.zsh kills it as hung.
# The first run in a FEX's prefix makes the prefix, which took about 20 s more.
typeset -A per_run=(smoke 5 counters 60) limit=(smoke 40 counters 120)
integer prefix_secs=20
typeset -A names=(engine "the engine's" mine mine off "mine, FEX_ENTRYHEADERLOOKUP=0 FEX_DISPATCHERL1LOOKUP=0")

typeset -a combos notes
for program in smoke counters; do
  for l1 in 0 unset; do
    for which in mine off engine; do
      combos+=($program:$which:$l1)
    done
  done
done
integer round_secs=0
for c in $combos; do round_secs+=$per_run[${c%%:*}]; done
integer budget=$(( rounds * round_secs + ${#names} * prefix_secs ))
if [[ $1 == --budget ]]; then
  # the longest time limit for the run started last, and a minute for the last report and
  # run.zsh's cleanup
  integer step_secs=$(( budget + ${${(@On)limit}[1]} + 60 ))
  print "seconds=$budget\nminutes=$(( (step_secs + 59) / 60 ))"
  exit 0
fi

typeset -A wines=(engine ${1:A} mine ${2:A} off ${2:A})
tests=${3:A} work=${4:A} out_file=${6:A}
integer start=$SECONDS t rc
here=${0:A:h}
smoke='wine-build smoke test: x86_64 FEX cases passed'

typeset -A runs passed crashed hung other secs
for c in $combos; do runs[$c]=0 passed[$c]=0 crashed[$c]=0 hung[$c]=0 other[$c]=0 secs[$c]=0; done

# the report so far, into <report> through a file beside it, so a reader never sees half of it
report() {
  local c program which l1
  local -a short
  for c in $combos; do (( runs[$c] >= rounds )) || short+=($c); done
  {
    print "### Crashes and hangs at process start\n"
    print "Round $round of $rounds, $(( SECONDS - start )) s so far. No run starts after $budget s:" \
      "$rounds rounds of $round_secs s and $(( ${#names} * prefix_secs )) s for the prefixes.\n"
    print "| program | FEX | FEX_DYNAMICL1CACHE | runs | passed | crashed | hung | failed otherwise | seconds a run |"
    print "|---|---|---|---|---|---|---|---|---|"
    for c in $combos; do
      program=${c%%:*} which=${${c#*:}%%:*} l1=${c##*:}
      print "| $program | $names[$which] | $l1 | $runs[$c] | $passed[$c] | $crashed[$c] | $hung[$c] | $other[$c] |" \
        "$(( runs[$c] ? secs[$c] / runs[$c] : 0 )) |"
    done
    print
    if (( ${#notes} )); then
      print "Runs that didn't pass (logs and samples in the stress artifact, under engine/, mine/ and off/):\n"
      print -rl -- $notes
    else
      print "No run failed."
    fi
    # in every report, so a step stopped at its time limit still leaves the list
    if (( finished && ${#short} )); then
      print "\nFewer than $rounds runs, as the budget ran out in round $round: the runs took longer" \
        "than planned (see hung and seconds a run).\n"
    elif (( ${#short} )); then
      print "\nFewer than $rounds runs so far, in round $round:\n"
    fi
    if (( ${#short} )); then
      for c in $short; do
        program=${c%%:*} which=${${c#*:}%%:*} l1=${c##*:}
        print -r -- "- $program, $names[$which], FEX_DYNAMICL1CACHE $l1: $runs[$c]"
      done
    fi
  } >"$out_file.new"
  mv -f "$out_file.new" "$out_file"
}
integer finished=0 round=1
report

for (( round = 1; round <= rounds; round++ )); do
  for c in $combos; do
    (( SECONDS - start < budget )) || break 2
    program=${c%%:*} which=${${c#*:}%%:*} l1=${c##*:}
    label=$program-l1-$l1-$round
    vars=()
    [[ $l1 == unset ]] || vars=(FEX_DYNAMICL1CACHE=$l1)
    [[ $which != off ]] || vars+=(FEX_ENTRYHEADERLOOKUP=0 FEX_DISPATCHERL1LOOKUP=0)
    mkdir -p "$work/$which"
    rc=0 t=$SECONDS
    if [[ $program == counters && $which != engine ]]; then
      out=$(GW2_TEST_READER="$tests/fexstats --header" "$here/run.zsh" "$wines[$which]" "$work/$which" "$label" $limit[counters] \
        'gw2 test counters: passed' "$tests/counters.exe" $vars FEX_PROFILESTATS=1 2>&1) || rc=$?
    elif [[ $program == counters ]]; then
      out=$(GW2_TEST_READER=/usr/bin/true "$here/run.zsh" "$wines[$which]" "$work/$which" "$label" $limit[counters] \
        'gw2 test counters: passed' "$tests/counters.exe" $vars 2>&1) || rc=$?
    else
      out=$("$here/run.zsh" "$wines[$which]" "$work/$which" "$label" $limit[smoke] "$smoke" "$tests/fex.exe" $vars 2>&1) || rc=$?
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
finished=1 round=$(( round > rounds ? rounds : round ))
report

