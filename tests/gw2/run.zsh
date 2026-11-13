#!/bin/zsh
# GitHub's runners, or a Mac where GW2_TEST_ALLOW_KILL=1. After each case this script kills every
# new process of this user that runs from the engine given (see below). Before it went by the
# command line alone, and on my Linux server it killed another agent's wait on a run. It now takes
# only executables under <wine>, so another project's Wine on the same user (the rented Mac) is
# safe, but it still refuses to start unless GITHUB_ACTIONS=true or GW2_TEST_ALLOW_KILL=1.
#
# gw2 (jdrly/gw2-on-mac#109): run one Win64 program under an engine, in a scratch prefix,
# without a window, the way Mach9's smoke test runs its programs (athei/mach9 at v0.3.1,
# bin/bundle-wine.sh: run_limited, run_fex, smoke_stop), and the way my Wine fork's
# tests/gw2/run.zsh does.
#
#   run.zsh <wine> <prefix> <label> <seconds> <want> <exe> [<VAR=value>...]
#
# <wine> is an unpacked engine (the release tarball's wine/). The prefix is booted the first time,
# with Wine's crash dialog off (HKCU\Software\Wine\WineDbg, ShowCrashDialog=0): a program that
# crashes then gets winedbg --auto's exception, registers, backtrace, modules and threads in its
# log and ends, where the dialog would hold it until the time limit (jdrly/gw2-on-mac#132).
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
# those with a variable of this run in their environment, whose executable is under <wine>: only
# this engine's processes, never another project's Wine on the same user. The variable alone doesn't do: on the
# runner, ps -E showed it for none of Wine's processes (run 43297396698), as Wine rewrites what
# ps reads there. A process left behind is one of them whose command names Wine or an .exe. Prints one line, and the log when the
# run failed; exits 1 then.
#
# When the program runs out of time, the watchdog first samples every Wine process of the run into
# <prefix>/<label>.hang.<pid>.log (jdrly/gw2-on-mac#132), and the failure names those files.
# When Wine reported the program with "wine: Unhandled …", the failure line says it crashed and
# quotes that line.
emulate -L zsh
setopt err_exit pipe_fail no_unset extended_glob

if [[ ${GITHUB_ACTIONS:-} != true && ${GW2_TEST_ALLOW_KILL:-} != 1 ]]; then
  print -u2 "run.zsh: not on a GitHub runner. It kills every new process of this user that runs from the engine given."
  print -u2 "Set GW2_TEST_ALLOW_KILL=1 only on a Mac where that engine is yours."
  exit 2
fi

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
# When hang_samples is set (only around the case's program), the watchdog first runs
# sample_hang "$hang_samples" <the command's pid>.
limited() {
  local s=$1 pid dog rc=0
  shift
  "$@" &
  pid=$!
  # a subshell, so it can run sample_hang. When limited stops it, its sleep and any sampling still
  # running (sample, lldb and lldb's own watchdog) end with it.
  (
    typeset -a kids
    trap 'kill_tree $nap $kids; exit 0' TERM
    sleep "$s" &
    nap=$!
    wait $nap
    [[ -z ${hang_samples:-} ]] || sample_hang "$hang_samples" $pid
    kill -KILL $pid 2>/dev/null || true
  ) &
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

# the pids of this run's processes: the ones since the script started, and the marked ones, whose
# executable is under <wine>. Another project's Wine on the same user has the same process names
# and may start while this runs, so neither a name nor a start time alone tells them apart
# (macOS's proc_pidpath gives the executable's real path; Wine rewrites the command line).
started() {
  local now=(${(f)"$(mine_now)"})
  local pids=(${(u)${now:|before}} $(marked))
  (( ${#pids} )) || return 0
  /usr/bin/python3 -I -c '
import ctypes, sys
proc = ctypes.CDLL("/usr/lib/libproc.dylib")
buf = ctypes.create_string_buffer(4096)
for pid in sys.argv[2:]:
    if proc.proc_pidpath(int(pid), buf, len(buf)) > 0 and buf.value.decode(errors="replace").startswith(sys.argv[1]):
        print(pid)' "$wine/" $pids
}

# pid and command of this run's processes whose command names Wine or an .exe, but not this
# script and its subshells, whose command line names the engine
winelike() {
  local pids=($(started))
  (( ${#pids} )) || return 0
  ps -ww -o pid=,command= -p ${(j:,:)pids} 2>/dev/null | grep -iE 'wine|\.exe' | grep -v '/run\.zsh' || true
}

# kill_tree <pid>...: the processes and everything they started, the children first
kill_tree() {
  local p
  for p in "$@"; do
    kill_tree $(pgrep -P $p 2>/dev/null || true)
    kill $p 2>/dev/null || true
  done
}

# sample_hang <base> <program pid>: where the case's program and each of this run's Wine processes
# stand, into <base>.<pid>.log: the pid and command, "(the case's program)" on the program's, then
# 3 seconds of macOS's sample. When sample gives no frames (FEX's threads may not unwind for it),
# lldb's "bt all" instead, for 60 seconds at most. All processes at once, so the case waits about as
# long as one of them takes. Each worker goes into the watchdog's kids, which its trap ends.
sample_hang() {
  local base=$1 program=$2 line pid out what
  local hang_samples= # lldb's own time limit samples nothing
  local lines=(${(f)"$(winelike)"})
  # the program's pid is the one limited started (env execs wine), whatever its command shows
  (( ${lines[(I) #$program *]} )) || lines+=("$program $(ps -ww -o command= -p $program 2>/dev/null || true)")
  for line in $lines; do
    line=${line## #} pid=${line%% *}
    out=$base.$pid.log what=
    if (( pid == program )); then what=" (the case's program)"; fi
    (
      print -r -- "pid $pid$what: ${line#* }"
      if sample $pid 3 -mayDie -file "$out.sample" >/dev/null 2>&1 && grep -qE '\(in [^)]+\)' "$out.sample" 2>/dev/null; then
        print "tool: sample"
        cat "$out.sample"
      else
        print "tool: lldb (sample gave no frames)"
        limited 60 lldb -p $pid --batch -o 'bt all' -o detach 2>&1 || print "lldb: exit $?"
      fi
      rm -f "$out.sample"
    ) >"$out" 2>&1 &
    kids+=($!)
  done
  wait
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
  # wineboot took 16 s in Mach9's release run. The first program boots the prefix, and this one
  # turns the crash dialog off (winedbg reads ShowCrashDialog there, programs/winedbg/crashdlg.c).
  rc=0
  run_limited 600 "$prefix/boot.log" "$wine/bin/wine" reg add 'HKCU\Software\Wine\WineDbg' \
    /v ShowCrashDialog /t REG_DWORD /d 0 /f || rc=$?
  if (( rc )); then
    print "FAIL  $label: the prefix boot (exit $rc)"
    sed 's/^/      /' "$prefix/boot.log"
    stop
    exit 1
  fi
fi

rc=0 failed=0
# how long the case's program ran, readers included, for the result line (what the stage costs)
integer t0=$SECONDS
rm -f "$prefix/$label".hang.*(N)
if [[ -n ${GW2_TEST_READER:-} ]]; then
  flag=$prefix/$label.done reader=$prefix/$label.reader
  rm -f "$flag" "$reader".*(N)
  (
    hang_samples=$prefix/$label.hang
    run_limited "$secs" "$log" GW2_TEST_FLAG="Z:${flag//\//\\}" "$@" "$wine/bin/wine" "$exe"
  ) &
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
  (
    hang_samples=$prefix/$label.hang
    run_limited "$secs" "$log" "$@" "$wine/bin/wine" "$exe"
  ) || rc=$?
fi

if (( rc != 0 )) || ! said "$want"; then
  why=
  (( rc != 137 )) || why=', the time limit'
  crash=(${(M)${(f)"$(loglines)"}:#wine: Unhandled *})
  (( ! ${#crash} )) || why+=", a crash: $crash[1]"
  print -r -- "FAIL  $label (exit $rc$why) after $(( SECONDS - t0 )) s"
  samples=("$prefix/$label".hang.<->.log(Nn))
  (( ! ${#samples} )) || print "      where it stood: ${(j:, :)${samples:t}} (the engine-tests artifact)"
  sed 's/^/      /' "$log"
  failed=1
elif (( failed )); then
  print "FAIL  $label: a read failed, after $(( SECONDS - t0 )) s"
  sed 's/^/      /' "$prefix/$label".reader.*(N)
else
  print "ok    $label, $(( SECONDS - t0 )) s"
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
