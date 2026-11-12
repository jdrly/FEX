#!/bin/zsh
# gw2 (jdrly/gw2-on-mac#109): run one Win64 program under an engine, in a scratch prefix,
# without a window, the way Mach9's smoke test runs its programs (athei/mach9 at v0.3.1,
# bin/bundle-wine.sh: run_limited, run_fex, smoke_stop), and the way my Wine fork's
# tests/gw2/run.zsh does.
#
#   run.zsh <wine> <prefix> <label> <seconds> <want> <exe> [<VAR=value>...]
#
# <wine> is an unpacked engine (the release tarball's wine/). The prefix is booted the first time.
# WINEDEBUG is -all unless a <VAR=value> sets it; each <VAR=value> is set for this run only. The
# program's output goes to <prefix>/<label>.log. It has to print the line <want> and exit 0
# within <seconds>.
#
# With GW2_TEST_READER=<native program> [<option>...], the program gets GW2_TEST_FLAG, a file to
# wait for (tests/gw2/gw2test.h). Each time it prints the line "gw2 test ready", the reader runs
# with the pids of this run's processes (see below), twice a second and for 10 seconds at most each time,
# while it exits 1 ("not there yet"). Its output goes to <prefix>/<label>.reader.<n>, n counting
# the reads from 1. Exit 0 is a read; anything else fails the run, with the exit in that file.
# Either way the file is created then, and the program deletes it and goes on.
#
# Afterwards the prefix's wineserver is stopped (30 seconds at most for each of its two calls), and
# no process this script started may be left.
# This run's processes are the ones of this user that weren't there when the script started, and
# those with a variable of this run in their environment. The variable alone doesn't do: on the
# runner, ps -E showed it for none of Wine's processes (run 43297396698), as Wine rewrites what
# ps reads there. A process left behind is one of them whose command names Wine or an .exe. Prints one line, and the log when the
# run failed; exits 1 then.
emulate -L zsh
setopt err_exit pipe_fail no_unset extended_glob

(( $# >= 6 )) || { print -u2 "usage: run.zsh <wine> <prefix> <label> <seconds> <want> <exe> [<VAR=value>...]"; exit 2 }
wine=${1:A} prefix=${2:A} label=$3 secs=$4 want=$5 exe=${6:A}
shift 6
export WINEPREFIX=$prefix WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
unset WINEARCH
# only in the environment of what run_limited starts, so ps, grep and this script don't carry it
mark=GW2_TEST_RUN=$$-${label//[^A-Za-z0-9]/_}
log=$prefix/$label.log

# limited <seconds> <command>...: the command, killed with SIGKILL (exit 137) when it runs longer.
# A watchdog process of its own does it, not an alarm in the command as in Mach9's run_limited:
# Wine blocks SIGALRM while it waits for the wineserver, so a stalled server would outlast one.
limited() {
  local s=$1 pid dog rc=0
  shift
  "$@" &
  pid=$!
  perl -e 'sleep shift; kill "KILL", shift' "$s" $pid &
  dog=$!
  wait $pid || rc=$?
  kill $dog 2>/dev/null || true
  wait $dog 2>/dev/null || true
  return $rc
}

# run_limited <seconds> <log> <command>...: the command with this run's mark, its output in <log>
run_limited() {
  local s=$1 out=$2
  shift 2
  limited "$s" env "$mark" "$@" >"$out" 2>&1
}

stop() {
  limited 30 "$wine/bin/wineserver" -k 2>/dev/null || true
  limited 30 "$wine/bin/wineserver" -w 2>/dev/null || true
}

# the pids of every process with the mark in its environment (macOS's ps -E prints it after the
# command)
marked() {
  local lines=(${(M)${(f)"$(ps -axEww -o pid=,command=)"}:#* $mark( *|)})
  print -l -- ${${lines## #}%% *}
}

# this user's processes now
mine_now() {
  ps -o pid= -U "$(id -u)" | tr -d ' '
}
before=(${(f)"$(mine_now)"})

# the pids of this run's processes: the ones since the script started, and the marked ones
started() {
  local now=(${(f)"$(mine_now)"})
  print -l -- ${(u)${now:|before}} $(marked)
}

# pid and command of this run's processes whose command names Wine or an .exe, but not this
# script and its subshells, whose command line names the engine
winelike() {
  local pids=($(started))
  (( ${#pids} )) || return 0
  ps -ww -o pid=,command= -p ${(j:,:)pids} 2>/dev/null | grep -iE 'wine|\.exe' | grep -v '/run\.zsh' || true
}

# the program's log so far, without the carriage returns; nothing before the program has
# created it (a failed redirection would end this script under err_exit)
loglines() {
  [[ -f $log ]] && tr -d '\r' <"$log"
  return 0
}

# whether the program's log has the line <line> yet (grep without -q, which would end the pipe
# early and fail it under pipe_fail)
said() {
  loglines | grep -xF -- "$1" >/dev/null
}

mkdir -p "$prefix"
if [[ ! -f $prefix/system.reg ]]; then
  # wineboot took 16 s in Mach9's release run
  rc=0
  run_limited 600 "$prefix/boot.log" "$wine/bin/wine" cmd /c exit || rc=$?
  if (( rc )); then
    print "FAIL  $label: the prefix boot (exit $rc)"
    sed 's/^/      /' "$prefix/boot.log"
    stop
    exit 1
  fi
fi

rc=0 failed=0
if [[ -n ${GW2_TEST_READER:-} ]]; then
  flag=$prefix/$label.done reader=$prefix/$label.reader
  rm -f "$flag" "$reader".*(N)
  run_limited "$secs" "$log" GW2_TEST_FLAG="Z:${flag//\//\\}" "$@" "$wine/bin/wine" "$exe" &
  job=$!
  integer reads=0 waiting=0
  # until the program ends: a new "gw2 test ready" starts the next read, which is retried until
  # the reader finds the object
  while kill -0 $job 2>/dev/null; do
    if (( !waiting )); then
      ready=(${(M)${(f)"$(loglines)"}:#gw2 test ready})
      if (( ${#ready} > reads )); then
        reads+=1
        waiting=1
      fi
    fi
    if (( waiting )); then
      diag=$(winelike)
      rr=0
      limited 10 ${=GW2_TEST_READER} $(started) >"$reader.$reads" 2>&1 || rr=$?
      if (( rr != 1 )); then
        if (( rr )); then
          print "the reader exited $rr" >>"$reader.$reads"
          failed=1
        fi
        touch "$flag"
        waiting=0
      fi
    fi
    sleep 0.5
  done
  wait $job || rc=$?
  if (( waiting )); then
    {
      print "the reader found no stats object for this read; this run's processes then:"
      print -r -- "$diag"
    } >>"$reader.$reads"
    failed=1
  fi
else
  run_limited "$secs" "$log" "$@" "$wine/bin/wine" "$exe" || rc=$?
fi

if (( rc != 0 )) || ! said "$want"; then
  print "FAIL  $label (exit $rc$( (( rc == 137 )) && print ', the time limit' ))"
  sed 's/^/      /' "$log"
  failed=1
elif (( failed )); then
  print "FAIL  $label: a read failed"
  sed 's/^/      /' "$prefix/$label".reader.*(N)
else
  print "ok    $label"
fi

stop
left=(${(f)"$(winelike)"})
if (( ${#left} )); then
  print "FAIL  $label: processes left behind:"
  print -l -- "      "${^left}
  kill -9 ${${left## #}%% *} 2>/dev/null || true
  failed=1
fi
(( failed == 0 ))
