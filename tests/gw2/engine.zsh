#!/bin/zsh
# gw2 (jdrly/gw2-on-mac#109): the test stage on the engine. Runs every case of tests/gw2/cases.zsh
# under the engine's own FEX and under mine, each engine in a scratch prefix of its own
# (tests/gw2/run.zsh).
#
#   engine.zsh <engine wine> <my wine> <tests dir> <work dir> <engine version> <my version>
#
# <engine wine> is the release tarball's wine/; <my wine> a copy with my libarm64ecfex.dll (also
# as xtajit64.dll) and libarm64ecfex.so. <tests dir> holds the programs: fex.exe (Mach9's
# smoke/fex.c), <name>.exe for each tests/gw2/<name>.c, and fexstats (tests/gw2/native). The
# versions are what the FEX that ran has to say: git describe of the engine's commit and of mine.
#
# cases.zsh is a list of calls to the four functions below, one line per case, run in order:
#
#   check  <label> <engines> <seconds> <program> <line> [<VAR=value>...]
#       <program> has to print <line> and exit 0 within <seconds>, with each <VAR=value> set.
#   stats  <label> <engines> <seconds> <program> <line> <pattern> [<VAR=value>...]
#       the same with FEX_PROFILESTATS=1. The program calls gw2_test_wait_for_reader()
#       (gw2test.h) each time it wants to be read, and fexstats reads its stats object then, into
#       <label>.reader.1, .2 and so on. One line of the last read has to match <pattern>, a zsh
#       pattern in which {version} stands for the version of the FEX that should be running. A
#       read fails the case when fexstats can't read the slots (another layout: exit 3). A case
#       that compares reads with each other can read the files itself after its stats line. The
#       summary shows each read's first 20 lines; the files are in the engine-tests artifact.
#   version <label> <engines> <seconds> <program> <line> [<VAR=value>...]
#       a stats case that reads only the header (fexstats --header, which reads any layout), and
#       the last read has to say the version of the FEX that should be running.
#   report <label> <case label> <pattern>
#       whether a line of that case's log matches <pattern>. Fails nothing; the answer, yes or
#       no, goes into the summary and into <work dir>/<engine>/<label>, for a later case to read.
#
# <engines> is both, engine or mine. Logs are <work dir>/<engine>/<label>.log, fexstats's output
# <label>.reader.<n>. Within cases.zsh, $prefix is that directory and $which the engine (engine or
# mine). Prints a Markdown summary; exits 1 when a check or stats case failed.
#
# The summary ends with a table of every case and whether it passed under each FEX. A case is
# open from its check or stats line (or begin_case, for a check in cases.zsh that runs run.zsh
# itself) until the next one, and fails when anything failed while it was open, its own checks
# included. A label that ends in -dev or -engine-like is a case of that rerun set
# (jdrly/gw2-on-mac#325), and the table names the set.
#
# With GW2_LANE=long or GW2_LANE=short in the environment it runs a part of the cases, so the
# workflow can run two of these side by side (see "lane" below). Unset, it runs every case.
emulate -L zsh
setopt err_exit pipe_fail no_unset extended_glob

(( $# == 6 )) || { print -u2 "usage: engine.zsh <engine wine> <my wine> <tests dir> <work dir> <engine version> <my version>"; exit 2 }
typeset -A wines=(engine ${1:A} mine ${2:A}) versions=(engine $5 mine $6) names=(engine "the engine's FEX" mine "my FEX")
typeset -A reports
typeset -a report_labels
tests=${3:A} work=${4:A}
here=${0:A:h}
integer failed=0
# GW2_LANE: unset, every case in this one process, as before. "long", only my FEX's cases of the
# programs in long_programs, in the prefix <work dir>/mine-long, plus the cases in both_lanes,
# whose answers their checks read. "short", every other case. The workflow runs the two side by
# side, each with a copy of my engine of its own: run.zsh tells a run's processes by the engine
# they run from.
lane=${GW2_LANE:-all}
[[ $lane == (all|long|short) ]] || { print -u2 "engine.zsh: GW2_LANE is long or short, or unset"; exit 2 }
long_programs=(counters.exe codebuf.exe)
both_lanes=(smoke-log hardware-tso)
# the labels of the check and stats cases this process ran, for the checks in cases.zsh
typeset -A ran

applies() { [[ $1 == both || $1 == "$which" ]] }

# each case's result, <label>/<engine> to ok or FAIL, and the labels in the order they first ran
typeset -A results
typeset -a result_labels
current= opened=0

# begin_case <label>: closes the case before it, and opens this one
begin_case() {
  end_case
  current=$1 opened=$failed
}

end_case() {
  [[ -n $current ]] || return 0
  if (( failed > opened )); then results[$current/$which]=FAIL; else results[$current/$which]=ok; fi
  (( ${result_labels[(Ie)$current]} )) || result_labels+=($current)
  current=
}

# in_lane <label> [<program>]: whether this process's lane runs that case
in_lane() {
  [[ $lane == all ]] && return 0
  (( ${both_lanes[(I)$1]} )) && return 0
  if (( ${long_programs[(I)${2:-}]} )); then [[ $lane == long ]]; else [[ $lane == short ]]; fi
}

check() {
  local label=$1 engines=$2 secs=$3 program=$4 line=$5
  shift 5
  applies $engines && in_lane $label $program || return 0
  ran[$label]=1
  begin_case $label
  "$here/run.zsh" "$wine" "$prefix" "$label" "$secs" "$line" "$tests/$program" "$@" || failed+=1
}

stats() {
  local label=$1 engines=$2 secs=$3 program=$4 line=$5 pattern=${6//\{version\}/$versions[$which]}
  shift 6
  applies $engines && in_lane $label $program || return 0
  ran[$label]=1
  begin_case $label
  GW2_TEST_READER="$tests/fexstats ${GW2_STATS_OPTIONS:-}" "$here/run.zsh" "$wine" "$prefix" "$label" "$secs" "$line" \
    "$tests/$program" "$@" FEX_PROFILESTATS=1 || failed+=1
  local files=($prefix/$label.reader.<->(Nn)) f
  integer lines
  # a read of counters.exe or slots.exe is 200 or 608 lines, and a step's summary takes 1024k
  for f in $files; do
    print "      read ${f##*.}:"
    head -n 20 "$f" | sed 's/^/        /'
    lines=$(wc -l <"$f")
    (( lines <= 20 )) || print "        ... $(( lines - 20 )) more lines in ${f:t}"
  done
  local last=(${(f)"$( (( ${#files} )) && cat "$files[-1]")"})
  if (( ${#files} && ${last[(I)$~pattern]} )); then
    print "ok    $label: fexstats said $pattern"
  else
    print "FAIL  $label: no line of fexstats's last read matches $pattern"
    failed+=1
  fi
}

version() {
  local label=$1 engines=$2 secs=$3 program=$4 line=$5
  shift 5
  GW2_STATS_OPTIONS=--header stats "$label" "$engines" "$secs" "$program" "$line" 'fex-<->-stats: *, FEX {version}' "$@"
}

report() {
  local label=$1 of=$2 pattern=$3 answer=no
  in_lane $label || return 0
  local lines=(${(f)"$(tr -d '\r' <"$prefix/$of.log")"})
  (( ${lines[(I)$~pattern]} )) && answer=yes
  print -- $answer >"$prefix/$label"
  reports[$label/$which]=$answer
  (( ${report_labels[(I)$label]} )) || report_labels+=($label)
  print "      $label: $answer"
}

passes=(engine mine) note=
if [[ $lane == long ]]; then passes=(mine) note=", the long cases"; fi
for which in $passes; do
  ran=()
  wine=$wines[$which] prefix=$work/$which
  [[ $lane != long ]] || prefix=$work/$which-long
  mkdir -p "$prefix"
  print "### Under $names[$which]$note\n"
  print '```'
  source "$here/cases.zsh"
  end_case
  print '```\n'
done

if (( ${#report_labels} )) && [[ $lane != long ]]; then
  print "### Reports\n"
  print "| | the engine's FEX | my FEX |"
  print "|---|---|---|"
  for label in $report_labels; do
    print "| $label | ${reports[$label/engine]:--} | ${reports[$label/mine]:--} |"
  done
  print
fi
title=Cases
[[ $lane != long ]] || title="Cases, the long lane"
print "### $title\n"
print "| case | set | the engine's FEX | my FEX |"
print "|---|---|---|---|"
for label in $result_labels; do
  in_set=
  case $label in
    (*-engine-like) in_set=engine-like ;;
    (*-dev) in_set=dev ;;
  esac
  print "| $label | $in_set | ${results[$label/engine]:--} | ${results[$label/mine]:--} |"
done
print
(( failed == 0 ))
