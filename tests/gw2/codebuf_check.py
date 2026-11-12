# codebuf_check.py (jdrly/gw2-on-mac#112): the verdict on one run of codebuf.exe.
#
#   python3 -I codebuf_check.py <mode> <log> <reader after pass 1> <reader after pass 2>
#
# <log> is the run's output, codebuf.exe's lines and Wine's stderr (where FEX's log goes with
# FEX_SILENTLOG=0). The two reader files are tests/gw2/native/fexstats's output while codebuf.exe
# waited after each pass, run.zsh's <label>.reader.1 and .2 ("thread <tid>: … jit_count=<n> …",
# the tid being the Windows thread id codebuf.exe prints). The mode is what
# FEX_INITIALCODEBUFFERMB was:
#   discard  unset: the first buffer is 16 MB, fills in pass 1, and pass 2 compiles at least 20%
#            of the functions again
#   keep     128: one buffer holds both passes, and pass 2 compiles fewer than 1% of them again
#   invalid  a value FEX doesn't take: as discard, plus exactly one error line from FEX
# In every mode pass 1 compiles every function at least once, both passes give the checksum the
# functions have to give, and FEX logs no error but the invalid mode's one.
# Prints one line per check; exits 1 when one fails.
import re
import sys

# FNV-1a over the 196608 functions' results, as codebuf.c computes it; worked out independently
# in Python from codebuf.c's value() and checked against a native run of codebuf.c on x86_64
CHECKSUM = "11be563cbba62325"
ERROR = re.compile(r"InitialCodeBufferMB is \d+, not 16, 32, 64 or 128: the code buffer starts at 16 MB")


def jit_count(path, tid):
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(rf"thread {tid}:.* jit_count=(\d+)", line)
        if m:
            return int(m.group(1))
    sys.exit(f"FAIL  no stats slot for thread {tid} in {path}")


def main(mode, log, first, second):
    if mode not in ("discard", "keep", "invalid"):
        sys.exit(f"FAIL  unknown mode {mode}")
    text = open(log, encoding="utf-8", errors="replace").read().replace("\r", "")
    m = re.search(r"^codebuf: tid (\d+) functions (\d+)$", text, re.M)
    if not m:
        sys.exit(f"FAIL  {log} has no 'codebuf: tid' line")
    tid, n = m.group(1), int(m.group(2))
    sums = re.findall(r"^codebuf: pass [12] checksum ([0-9a-f]{16})$", text, re.M)
    a, b = jit_count(first, tid), jit_count(second, tid)
    again = b - a
    errors = len(ERROR.findall(text))

    checks = [
        (a >= n, f"pass 1 compiled {a} blocks, at least the {n} functions"),
        (sums == [CHECKSUM, CHECKSUM], f"checksums {' '.join(sums) or 'none'}, both {CHECKSUM}"),
        (errors == (1 if mode == "invalid" else 0), f"{errors} InitialCodeBufferMB error lines"),
    ]
    if mode == "keep":
        checks.append((again * 100 < n, f"pass 2 compiled {again} blocks again, fewer than 1% of {n}"))
    else:
        checks.append((again * 5 >= n, f"pass 2 compiled {again} blocks again, at least 20% of {n}"))
    failed = 0
    for ok, what in checks:
        print(f"{'ok   ' if ok else 'FAIL '} {mode}: {what}")
        failed += not ok
    return 1 if failed else 0


if __name__ == "__main__":
    if len(sys.argv) != 5:
        sys.exit("usage: codebuf_check.py discard|keep|invalid <log> <reader after pass 1> <reader after pass 2>")
    sys.exit(main(*sys.argv[1:]))
