#!/usr/bin/env bash
# Runs the Linux CI steps of .github/workflows/build.yml in Docker, building out of
# tree: the build and the CPM source cache live in named volumes, the checkout is
# mounted read-write at /src and its own build/ is never touched.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

docker build -f "$repo/Docker/Linux.Dockerfile" -t epm-linux-ci "$repo/Docker"

docker run --rm \
    -v "$repo:/src" \
    -v epm-linux-build:/work \
    -v epm-cpm-cache:/cpm \
    -e CPM_SOURCE_CACHE=/cpm \
    -w /work \
    epm-linux-ci \
    bash -c '
        set -euo pipefail
        cmake --version
        clang++ --version
        cmake -S /src -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_LINKER_TYPE=LLD -DEPM_CI_BUILD=ON
        cmake --build /work/build --parallel
        /work/build/Apps/epm/epm --version
        ctest --test-dir /work/build --output-on-failure "$@"
    ' bash "$@"
