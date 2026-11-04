#!/usr/bin/env bash
# Clones a branch of epm into a fresh directory with an empty cache, builds
# it and runs every test. Used to check a branch on another machine.
#
#   Scripts/check-clean-build.sh <work directory> [branch] [repository] [cmake flags...]
set -euo pipefail

work=$1
branch=${2:-lockstep}
repository=${3:-https://github.com/jamierpond/epm}
shift $(( $# < 3 ? $# : 3 ))

rm -rf "$work"
mkdir -p "$work"
git clone --quiet --branch "$branch" "$repository" "$work/src"
cd "$work/src"
git log --oneline -1

export EPM_CACHE="$work/cache"
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release "$@" > "$work/configure.log" 2>&1 \
    || { tail -40 "$work/configure.log"; exit 1; }
cmake --build build > "$work/build.log" 2>&1 || { tail -40 "$work/build.log"; exit 1; }
ctest --test-dir build -j4 --output-on-failure > "$work/test.log" 2>&1 || true
grep -E "tests passed|\*\*\*Failed|Not Run" "$work/test.log" | tail -20
