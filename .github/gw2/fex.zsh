#!/bin/zsh
# gw2 (jdrly/gw2-on-mac#109): build FEX's four files the way Mach9 0.3.1's CI built the engine's
# (athei/mach9 at v0.3.1: bin/build-fex.sh, bin/lib/profile.sh, bin/build-wine.sh, the strip in
# bin/bundle-wine.sh; the log of the run that compiled them is athei/mach9 run 42746773278).
#
#   fex.zsh checkout <commit>       move $SRC to <commit>, every submodule at its recorded commit
#   fex.zsh build <dist> [<version>] configure and build the three trees in $BUILD, install each
#                                   into a scratch prefix as Mach9 does, and put the four files in
#                                   <dist>: aarch64-windows/ and aarch64-unix/ as the engine has
#                                   them (the DLLs through llvm-strip --strip-all), unstripped/ the
#                                   two DLLs as linked. Both DLLs have to say <version>, or without
#                                   it FEX-<tag>-<n>-g<the commit's first 7 digits>.
#   fex.zsh record <dir>            ninja's dependency records, link inputs and compile commands of
#                                   the three trees into <dir>, for compare.py record
#   fex.zsh notices <file>          the licences of what the four files are built from, besides
#                                   FEX's own: every directory under External/ whose files an
#                                   object of theirs includes or links, as $RECORD says
#
# $SRC and $BUILD are the engine's own paths. Mach9 compiled through ccache with CCACHE_BASEDIR
# set to its checkout's parent, so the compiler saw paths relative to the tree
# (../../../src-fex/…), and those reach the objects. $MINGW_DIR is llvm-mingw, where Mach9's CI
# put it. The configure arguments are build-fex.sh's.
emulate -L zsh
setopt err_exit pipe_fail no_unset extended_glob

: ${SRC:=/Users/runner/work/mach9/mach9/src-fex} ${BUILD:=/Users/runner/work/mach9/mach9/build/fex}
: ${MINGW_DIR:=/Users/runner/llvm-mingw}
trees=(wow64 arm64ec unixlib)
# the CMake targets of the four files, by tree
typeset -A targets=(wow64 wow64fex arm64ec arm64ecfex unixlib 'wow64fex_unixlib arm64ecfex_unixlib')

# what profile.sh and build-wine.sh set before build-fex.sh runs: the floor, llvm-mingw appended
# (its bin/ has an unprefixed clang that mustn't stand in for Apple's), ccache's base directory
# and its compiler check
export MACOSX_DEPLOYMENT_TARGET=26.4
path+=("$MINGW_DIR/bin")
export CCACHE_BASEDIR=${SRC:h}
export CCACHE_COMPILERCHECK="string:cc=$(/usr/bin/clang --version 2>/dev/null | head -n 1) mingw=$("$MINGW_DIR/bin/clang" --version 2>/dev/null | head -n 1)"
ccache=$(command -v ccache)
launcher=(-DCMAKE_C_COMPILER_LAUNCHER="$ccache" -DCMAKE_CXX_COMPILER_LAUNCHER="$ccache")

checkout() {
  git -C "$SRC" checkout --quiet --detach "$1"
  git -C "$SRC" submodule update --init --recursive
  # a leading '+', '-' or 'U': a submodule not at the recorded commit, not checked out, or in conflict
  if git -C "$SRC" submodule status --recursive | grep '^[-+U]'; then
    print -u2 "::error::the submodules aren't at $1's commits"
    exit 1
  fi
  print "$(git -C "$SRC" rev-parse HEAD): $(git -C "$SRC" describe --abbrev=7)"
}

configure() {
  local target triple
  for target in wow64 arm64ec; do
    case $target in
      wow64) triple=aarch64-w64-mingw32 ;;
      arm64ec) triple=arm64ec-w64-mingw32 ;;
    esac
    cmake -S "$SRC" -B "$BUILD/$target" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE="$SRC/Data/CMake/toolchain_mingw.cmake" \
      -DMINGW_TRIPLE=$triple \
      -DCMAKE_INSTALL_LIBDIR=lib/wine/aarch64-windows \
      -DENABLE_LTO=False -DENABLE_ASSERTIONS=False -DENABLE_JEMALLOC_GLIBC_ALLOC=False \
      -DBUILD_TESTING=False -DTUNE_ARCH=generic -DTUNE_CPU=none -DRANGES_NATIVE=OFF \
      -DENABLE_CCACHE=OFF "${launcher[@]}"
  done
  cmake -S "$SRC/Source/Windows/UnixLib" -B "$BUILD/unixlib" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=26.4 \
    -DCMAKE_INSTALL_LIBDIR=lib/wine/aarch64-unix \
    "${launcher[@]}"
}

build() {
  local dist=$1 want=${2:-} commit t f found install=$BUILD/install
  commit=$(git -C "$SRC" rev-parse HEAD)
  # every time: CMake runs git describe when it configures, and the version goes into the files
  configure
  for t in $trees; do cmake --build "$BUILD/$t"; done
  rm -rf "$install" "$dist"
  for t in $trees; do cmake --install "$BUILD/$t" --prefix "$install" >/dev/null; done
  mkdir -p "$dist"/{aarch64-windows,aarch64-unix,unstripped}
  for f in libwow64fex libarm64ecfex; do
    cp "$install/lib/wine/aarch64-windows/$f.dll" "$dist/unstripped/"
    "$MINGW_DIR/bin/llvm-strip" --strip-all -o "$dist/aarch64-windows/$f.dll" "$dist/unstripped/$f.dll"
    # bundle-wine.sh refuses a strip that loses Wine's builtin marker (grep without -q, which
    # could end the pipe early and fail it under pipe_fail)
    head -c 128 "$dist/aarch64-windows/$f.dll" | grep -a "Wine builtin DLL" >/dev/null ||
      { print -u2 "::error::$f.dll lost Wine's builtin marker in the strip"; exit 1 }
    cp "$install/lib/wine/aarch64-unix/$f.so" "$dist/aarch64-unix/"
  done
  rm -rf "$install"
  # FEX's version, from git describe (CMakeLists.txt, GIT_DESCRIBE_STRING), in both DLLs. Only
  # the whole form: the engine's files also hold an unrelated "FEX-26104".
  for f in "$dist"/aarch64-windows/*.dll; do
    found=(${(f)"$(LC_ALL=C grep -aoE 'FEX-[0-9]+(\.[0-9]+)?-[0-9]+-g[0-9a-f]{7,}' "$f" | sort -u || true)"})
    if [[ -n $want ]]; then
      [[ ${#found} == 1 && $found[1] == "$want" ]] ||
        { print -u2 "::error::${f:t} says ${found:-no version}, not $want"; exit 1 }
    else
      [[ ${#found} == 1 && $found[1] == FEX-*-g${commit[1,7]} ]] ||
        { print -u2 "::error::${f:t} says ${found:-no version}, not FEX-<tag>-<n>-g${commit[1,7]}"; exit 1 }
    fi
    print "${f:t} says $found[1], for $commit"
  done
  ls -lR "$dist"
}

record() {
  local dir=$1 t
  mkdir -p "$dir"
  for t in $trees; do
    ninja -C "$BUILD/$t" -t deps >"$dir/$t.deps"
    ninja -C "$BUILD/$t" -t inputs ${=targets[$t]} >"$dir/$t.inputs"
    ninja -C "$BUILD/$t" -t compdb >"$dir/$t.compdb.json"
  done
}

notices() {
  local out=$1 names name dir lic url commit
  : ${RECORD:?the record directory of compare.py record}
  # the directories under External/ that an object's inputs name (headers count: they carry code)
  names=(${(f)"$(python3 -I -c '
import json, sys
rec = json.load(open(sys.argv[1]))
names = set()
for objs in rec["trees"].values():
    for o in objs.values():
        for i in o["inputs"]:
            p = i.split(":", 1)[1].split("/")
            if i.startswith("src:") and p[0] == "External" and len(p) > 2:
                names.add(p[1])
print("\n".join(sorted(names)))' "$RECORD/record.json")"})
  (( ${#names} )) || { print -u2 "::error::no External/ directory among the objects' inputs"; exit 1 }
  {
    print "FEX's four files are built from FEX (LICENSE, MIT) and the code below, which they"
    print "include or link statically. Each one's licence follows, as it is in FEX's tree at"
    print "$(git -C "$SRC" rev-parse HEAD)."
    for name in $names; do
      dir=$SRC/External/$name
      url=$(git -C "$SRC" config -f .gitmodules --get "submodule.External/$name.url" 2>/dev/null || true)
      commit=$(git -C "$SRC" ls-tree HEAD "External/$name" | awk '$2 == "commit" { print $3 }')
      print "\n================================================================================"
      print "External/$name${url:+ ($url${commit:+ at $commit})}${url:-, in FEX's tree}"
      print "================================================================================\n"
      lic=($dir/(#i)(licen[cCsS]e|copying)*(N.))
      if (( ${#lic} )); then
        cat $lic[1]
      elif [[ -f $dir/src/extF80_add.c ]]; then
        # SoftFloat 3e: FEX's copy has no licence file; each source file starts with it
        sed -n '1,/^=*\*\/$/p' "$dir/src/extF80_add.c"
      else
        print "No licence file in FEX's tree."
        print "::warning::External/$name has no licence file" >&2
      fi
    done
  } >"$out"
  print "${(j:, :)names}"
}

case ${1:-}:$# in
  checkout:2 | build:2 | build:3 | record:2 | notices:2) "$@" ;;
  *) print -u2 "usage: fex.zsh checkout <commit> | build <dist> [<version>] | record <dir> | notices <file>"; exit 2 ;;
esac
