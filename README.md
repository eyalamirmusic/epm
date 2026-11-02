# epm

epm is a package manager for C++ projects that build with CMake. A project
declares its dependencies with `epm_add_package(...)` calls, in the same shape as
CPM.cmake's `CPMAddPackage`. A small native binary (`epm`) fetches each
dependency into a shared per-user cache. Where it can, it also builds the
dependency once per toolchain, Debug and Release, and hands the consumer
imported targets that point at the cached libraries, so the dependency is never
`add_subdirectory`'d again. The target names are the same either way, so the
consumer's CMake does not change.

The longer-term design (remote binary caches, lockfiles, transitive
dependencies, a CPM compatibility shim) is in [PLAN.md](PLAN.md). What exists
today, including the files exchanged between `epm.cmake` and the binary and the
cache layout, is described in [Docs/PoC.md](Docs/PoC.md).

## Status

This is a proof of concept. It is exercised with
[eacp](https://github.com/eyalamirmusic/eacp) as the test dependency, on macOS,
Windows and Linux in CI (see below). File formats, cache layout and the CMake
interface may all change.

## Quick start

```cmake
cmake_minimum_required(VERSION 3.31)
project(HelloEacp CXX)

include(path/to/epm/CMake/epm.cmake)

epm_add_package(
        NAME eacp
        GITHUB_REPOSITORY eyalamirmusic/eacp
        GIT_TAG main
        MODE auto                       # auto | prebuilt | source
        OPTIONS "EACP_BUILD_GRAPHICS OFF" "EACP_BUILD_WEBVIEW OFF"
        "EACP_ENABLE_TESTS OFF" "EACP_ENABLE_EXAMPLES OFF")

add_executable(hello Main.cpp)
target_compile_features(hello PRIVATE cxx_std_20)
target_link_libraries(hello PRIVATE eacp-core)
```

This is `Examples/HelloEacp` in this repository. From the repository root:

```bash
cmake -S Examples/HelloEacp -B build-hello
cmake --build build-hello
./build-hello/hello
```

The first configure fetches epm itself (see below) and then builds eacp, in
Debug and Release, into the cache. Later configures, in any build directory,
reuse both and take about a second.

## Getting the epm binary

`epm.cmake` pins an epm version and finds the binary when it is included,
in this order:

1. `-DEPM_EXECUTABLE=/path/to/epm`, if given.
2. A binary fetched earlier, under `<cache>/tool/<ref>/`.
3. The pinned release binary, downloaded from
   [GitHub Releases](https://github.com/eyalamirmusic/epm/releases) on macOS,
   Linux (x64, arm64) and Windows (x64, arm64), checked against the SHA-256
   pinned in `epm.cmake` and unpacked into `<cache>/tool/<ref>/`. This takes a
   few seconds and prints one line (`-- epm: downloaded epm 0.1.1 (...)`).
4. Otherwise, or if the download, the hash check or a test run of the binary
   fails: download the pinned source tag and build it on the host, once per
   machine, into the same place. This prints one line and takes a minute or so.
   The build ignores the consumer's toolchain, so it also works when the
   consumer targets iOS or Android.
5. `epm` on `PATH`, if the source could not be downloaded either.

The cache is `~/Library/Caches/epm` on macOS, `$XDG_CACHE_HOME/epm` or
`~/.cache/epm` on Linux, `%LOCALAPPDATA%\epm` on Windows, or wherever `EPM_CACHE`
(CMake or environment variable) points. `-DEPM_BOOTSTRAP_NO_DOWNLOAD=ON` always
builds the pinned version from source, `EPM_SOURCE_REF` (or `EPM_SOURCE_URL`)
builds another tag, branch or commit from source, `EPM_BOOTSTRAP_SOURCE_DIR`
builds from a local checkout, and `EPM_VERBOSE=ON` shows the full output.
Details are in Docs/PoC.md §8.

Every release also carries an `epm.cmake` asset with its version and hashes
already pinned, so a project can vendor that single file. The copy on the
`nightly` prerelease pins the nightly binaries.

## Building and installing epm

epm needs CMake 3.31 and a C++20 compiler. Its own dependencies (eacp, Miro) are
fetched with CPM and linked statically.

```bash
cmake -S . -B build && cmake --build build && cmake --install build --prefix ~/.local
```

This installs `bin/epm` and `share/epm/cmake/epm.cmake`, nothing else. Tests run
with `ctest --test-dir build` (`EPM_ENABLE_TESTS` is ON by default when epm is
the top-level project).

## Continuous integration

[![CI](https://github.com/eyalamirmusic/epm/actions/workflows/ci.yml/badge.svg)](https://github.com/eyalamirmusic/epm/actions/workflows/ci.yml)

Every push and pull request runs `.github/workflows/ci.yml` on five targets, all
Release builds with Ninja:

| Platform          | Runner             | Compiler                            |
|-------------------|--------------------|-------------------------------------|
| `macos-universal` | `macos-15`         | Apple clang, `arm64;x86_64`         |
| `windows-x64`     | `windows-latest`   | clang-cl (MSVC ABI)                 |
| `windows-arm64`   | `windows-11-arm`   | clang-cl (MSVC ABI)                 |
| `linux-x64`       | `ubuntu-24.04`     | clang, lld                          |
| `linux-arm64`     | `ubuntu-24.04-arm` | clang, lld                          |

Each job builds epm, runs `ctest`, then configures and runs `Examples/HelloEacp`
with the binary it just built (`MODE auto`, so eacp is fetched and built Debug
and Release into a cache inside the workspace), configures it once more with no
epm at all and a fresh cache to check that the pinned release binary is
downloaded, and uploads the package as a workflow artifact.

`.github/workflows/release.yml` builds the same packages on pushes to `main` and
`release/**` and on `v*` tags (the list is at the top of the file):
`epm-<version>-<platform>.tar.gz` (`.zip` on Windows), holding `bin/epm` and
`share/epm/cmake/epm.cmake`, a `SHA256SUMS` file, and an `epm.cmake` with
those hashes pinned (see above). A branch push replaces
the assets of the rolling
[`nightly`](https://github.com/eyalamirmusic/epm/releases/tag/nightly)
prerelease; a `v*` tag publishes them on the
[release](https://github.com/eyalamirmusic/epm/releases) of that tag. Both
workflows share their build matrix through `.github/workflows/build.yml`.

The Linux binaries are built on Ubuntu 24.04 and need glibc 2.38 or newer and
`libcurl.so.4`; the Windows binaries need the Visual C++ runtime.
