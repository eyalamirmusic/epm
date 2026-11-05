# Mirrors the linux-x64/linux-arm64 jobs of .github/workflows/build.yml: ubuntu-24.04
# with clang, lld, ninja and libcurl, plus a CMake as new as the runner's (apt has 3.28).
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        clang lld ninja-build libcurl4-openssl-dev \
        git ca-certificates python3-pip \
    && rm -rf /var/lib/apt/lists/* \
    && pip3 install --no-cache-dir --break-system-packages 'cmake>=3.31' \
    && git config --system --add safe.directory '*' \
    && git config --system user.name 'epm CI' \
    && git config --system user.email 'ci@epm.invalid'
