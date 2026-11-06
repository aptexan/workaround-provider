#!/bin/bash
# Is the build itself sound? The other checks run an already-built provider; this one looks at how it is built.
# It catches the failure where build/ gets configured against the wrong packages (for example a Windows Anaconda found
# through the inherited WSL PATH) and then fails with hundreds of "incompatible version of Protobuf" compile errors.
#   1. the real build/: its cache has no Windows packages, all dependencies come from ONE prefix, and
#      `cmake --build` succeeds (incremental: a no-op when up to date)
#   2. the project's own protections work, tried in scratch folders: a fresh configure picks the right prefix, never
#      uses Windows packages even with no prefix given, and refuses packages from two different installations
# BUILD_DIR and SOURCE_DIR default to this repository; the negative tests point them at a copy. Details: TESTING.md.
. "$(dirname "$0")/lib.sh"
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
BUILD_DIR=${BUILD_DIR:-$SOURCE_DIR/build}
CACHE=$BUILD_DIR/CMakeCache.txt
[ -f "$CACHE" ] || { echo "no $CACHE: configure and build first (BUILDING.md)"; exit 2; }

# The directories CMake found each dependency in, from a cache file:  "gRPC /home/x/.local/grpc-openssl/lib/cmake/grpc" ...
package_dirs() { grep -E '^(gRPC|Protobuf|absl|utf8_range|re2|c-ares)_DIR:' "$1" | sed -E 's/^(.*)_DIR:[A-Z]*=/\1 /'; }
# .../lib/cmake/<package>  or  .../lib/<arch>/cmake/<package>  ->  the installation prefix
prefixes_of() { package_dirs "$1" | cut -d' ' -f2 | sed -E 's|/lib[^/]*(/[^/]+)?/cmake/[^/]+/?$||' | sort -u; }
count() { grep -c . <<< "$1"; }                       # number of non-empty lines
PREFIX=$(sed -n 's/^CMAKE_PREFIX_PATH:[A-Z]*=//p' "$CACHE")
# A configure without -DCMAKE_PREFIX_PATH (the short form, or the standard-prefix fallback) records none: use the prefix
# the dependencies were actually found in.
[ -n "$PREFIX" ] || PREFIX=$(prefixes_of "$CACHE" | head -1)
configure() {  # configure <folder> [cmake options]: configure a scratch build of the project; the log is $WORK/<folder>.log
  local dir=$WORK/$1; shift
  cmake -S "$SOURCE_DIR" -B "$dir" "$@" >"$dir.log" 2>&1
}

echo "== the real build folder"
expect "no dependency was found on a Windows drive (/mnt/...)" "$(package_dirs "$CACHE" | grep -c ' /mnt/')" 0
expect "all dependencies come from one installation"          "$(count "$(prefixes_of "$CACHE")")" 1
expect "cmake --build succeeds"                               "$(cmake --build "$BUILD_DIR" --parallel 4 >"$WORK/build.log" 2>&1 && echo yes || echo no)" yes

echo "== the project's protections, in scratch folders"
configure good -DCMAKE_PREFIX_PATH="$PREFIX"
expect "a fresh configure with the prefix works"              "$([ -f "$WORK/good/Makefile" ] && echo yes || echo no)" yes
expect "  ... and every dependency comes from that prefix"    "$(prefixes_of "$WORK/good/CMakeCache.txt")" "$PREFIX"

if negatives; then
  # A prefix that has no gRPC at all: CMake then falls back to searching PATH, which under WSL holds the Windows install
  # directories. That search must not reach Windows. (With NO prefix the project uses its standard one, see below, so only
  # a given-but-wrong prefix reaches the PATH search.)
  mkdir "$WORK/empty-prefix"
  configure wrongprefix -DCMAKE_PREFIX_PATH="$WORK/empty-prefix"
  expect "with a prefix that lacks gRPC, no Windows package is ever used" "$(package_dirs "$WORK/wrongprefix/CMakeCache.txt" 2>/dev/null | grep -c ' /mnt/')" 0

  configure noprefix
  expect "with no prefix at all, no Windows package is used either" "$(package_dirs "$WORK/noprefix/CMakeCache.txt" 2>/dev/null | grep -c ' /mnt/')" 0
fi

# The order of configures must not matter. A configure from the command line records the compiler CMake chose (often
# /usr/bin/cc); VS Code's CMake Tools then asks for /usr/bin/gcc, CMake sees a changed compiler, deletes the cache and
# re-runs WITHOUT the -D options, so the prefix is lost. The project has to find its gRPC anyway. This replays that.
GCC=$(command -v gcc); GXX=$(command -v g++)
if [ -n "$GCC" ] && [ -n "$GXX" ]; then
  configure order -DCMAKE_PREFIX_PATH="$PREFIX"                                     # first, "from the command line"
  cmake -S "$SOURCE_DIR" -B "$WORK/order" -DCMAKE_PREFIX_PATH:STRING="$PREFIX" -DCMAKE_IGNORE_PREFIX_PATH:STRING=/mnt/c \
        -DCMAKE_C_COMPILER:FILEPATH="$GCC" -DCMAKE_CXX_COMPILER:FILEPATH="$GXX" -Wno-unused-cli \
        -G "Unix Makefiles" >"$WORK/order2.log" 2>&1                                # then what CMake Tools runs
  expect "configure from the command line, then from VS Code, works" "$([ $? = 0 ] && echo yes || echo no)" yes
  expect "  ... and every dependency still comes from the prefix"  "$(prefixes_of "$WORK/order/CMakeCache.txt")" "$PREFIX"
else
  echo "SKIP  command line then VS Code (gcc/g++ not found)"
fi

if negatives; then
  # Two installations on purpose: take Protobuf from ANOTHER prefix that has one (the BoringSSL gRPC, if it is installed).
  OTHER=$(ls -d "$HOME"/.local/grpc*/lib/cmake/protobuf 2>/dev/null | grep -v "^$PREFIX/" | head -1)
  if [ -n "$OTHER" ]; then
    configure mixed -DCMAKE_PREFIX_PATH="$PREFIX" -DProtobuf_DIR="$OTHER"
    expect "packages from two installations are refused"         "$(grep -q 'different installations' "$WORK/mixed.log" && echo yes || echo no)" yes
  else
    echo "SKIP  packages from two installations are refused (no second install with Protobuf to try)"
  fi
fi
summary
