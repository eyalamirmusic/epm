#!/usr/bin/env bash
# Runs Scripts/check-clean-build.sh inside a clean Ubuntu 24.04 container, the
# image GitHub's Linux runners use, with clang.
#
#   Scripts/check-linux-docker.sh <linux/amd64|linux/arm64> [branch]
set -euo pipefail

platform=$1
branch=${2:-lockstep}
scripts="$(cd "$(dirname "$0")" && pwd)"

docker run --rm --platform "$platform" -v "$scripts:/scripts:ro" ubuntu:24.04 bash -c "
    set -e
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq > /dev/null
    apt-get install -y -qq git clang lld python3-pip libcurl4-openssl-dev \
        pkg-config ca-certificates ninja-build > /dev/null
    pip3 install -q --break-system-packages cmake==3.31.6
    bash /scripts/check-clean-build.sh /work $branch https://github.com/jamierpond/epm \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_LINKER_TYPE=LLD"
