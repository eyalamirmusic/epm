# epm

epm is a package manager for C++ projects that build with CMake. A project
declares its dependencies with `epm_add_package(...)` calls, in the same shape as
CPM.cmake's `CPMAddPackage`. A small native binary (`epm`) fetches each
dependency into a shared per-user cache. Where it can, it also builds the
dependency once per toolchain, in the configurations the consumer builds, and
hands the consumer imported targets that point at the cached libraries, so the
dependency is never `add_subdirectory`'d again. The target names are the same either way, so the
consumer's CMake does not change.

The longer-term design (remote binary caches, lockfiles, transitive
dependencies, a CPM compatibility shim) is in [PLAN.md](PLAN.md). What exists
today is described below.

## Status

This is a proof of concept. It is exercised with
[eacp](https://github.com/eyalamirmusic/eacp) as the test dependency, on macOS,
Windows and Linux in CI (see below). File formats, cache layout and the CMake
interface may all change.

## Quick start

```cmake
cmake_minimum_required(VERSION 3.31)
set(EPM_CONFIGS Release) # only Release archives of eacp; Debug builds of hello link them on multi-config generators (missing configs map to Release)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/cmake/get_epm.cmake")  # vendored from this repository's CMake/
project(HelloEacp CXX)

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

This is `Examples/HelloEacp` in this repository, with its `epm.cpp` beside it.
From the repository root:

```bash
cmake -S Examples/HelloEacp -B build-hello
cmake --build build-hello
./build-hello/hello
```

The first configure fetches epm itself (see below) and then builds eacp's
Release configuration into the cache. Later configures, in any build directory
and any build type, reuse it and take about a second. Without the
`EPM_CONFIGS` line, epm builds the build type you configured instead, and a
configure in another build type adds just that configuration.

## Declaring dependencies in epm.cpp

A project can instead list its dependencies in `epm.cpp` beside the top-level
`CMakeLists.txt`. epm compiles the file with the configure's own C++ compiler,
against headers embedded in the epm binary and written to
`<cache>/recipe-sdk/` on first use, loads it into its own process and calls
`describe`, so the list is C++ (macOS and Linux only; Windows has no recipes
yet):

```cpp
#include <epm/Recipe/Recipe.h>

using namespace epm;

struct App : Recipe
{
    void describe(Project& project, const Env& env) override
    {
        setMode(project.add("fmt", "fmtlib/fmt", "11.0.2"), "source");
        project.addSystem("Threads");
        project.overridePackage("shared").commit = "<commit>";
    }
};

EPM_RECIPE(App)
```

`find_package(fmt)` then loads it. A dependency's own `epm.cpp` is compiled
and run in the same way at configure time, from the checkout the lock pins,
so its Merkle hash covers the recipe code that runs. `epm_add_recipe(<target>)`
builds the recipe as a module of the project's own build, so an IDE indexes it
and a debugger can step through `describe`.

## Configuring what gets built

Two cache variables, set with `-D` or before the first `epm_add_package`:

- `EPM_CONFIGS` (default `auto`): which configurations of each package to build
  and cache. `auto` builds your `CMAKE_BUILD_TYPE` with a single-config
  generator (Ninja, Makefiles; empty, `RelWithDebInfo` and `MinSizeRel` mean
  Release) and Debug + Release with a multi-config one (Xcode, Visual Studio).
  An explicit list such as `-DEPM_CONFIGS="Debug;Release"` builds those.
  Configurations are added to a cached build as they are needed; one that is
  there is never rebuilt.
- `EPM_ABI_MATCH` (default `compatible`): which cached build may be used.
  `compatible` accepts one made with the same compiler family and major version
  (so Xcode's clang and the command-line tools' clang share builds), the same
  deployment target, C++ standard, options and source; `strict` requires every
  recorded detail, compiler path and full version included; `any` only the
  platform, options and source. A build that is not an exact match is named on
  the configure line:
  `-- epm: eacp (prebuilt, macos-arm64, 1a2b3c4d5e6f, built with AppleClang 17.0.0.17000013, current 17.0.0.17000319)`.

`epm cache ls` lists what the cache holds.

## Locking the dependency tree

Every package is pinned in `epm-lock.json`, beside the top-level `CMakeLists.txt`
(`EPM_LOCK_FILE` moves it), to a commit and a Merkle hash of its files (path,
type and SHA-256 of each entry, `.git` and execute bits left out), with one
root hash over all of them. epm never builds unless the lock is satisfied. To
capture `FetchContent_MakeAvailable` too, load epm during `project()`:

```cmake
cmake_minimum_required(VERSION 3.31)
set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES "${CMAKE_CURRENT_LIST_DIR}/cmake/get_epm.cmake")
project(HelloEacp CXX)
```

`CPMAddPackage`, `CPMFindPackage`, `CPMDeclarePackage` and
`CPMGetPackage` are epm's, `include(CPM)` finds an empty stub, and a
dependency's own CPM.cmake returns before defining anything, so the packages
dependencies ask for land in the same lock. Child builds of prebuilt packages
load epm too. The lock is an input of the configure, so a build after it is
edited, pulled or deleted reconfigures. `EPM_LOCK_MODE` is `locked` (the default: a missing lock is
written on the first configure, a lock that disagrees with the CMake stops the
configure), `update` (re-resolve every ref and rewrite the lock) or `offline`
(the cache only); with `EPM_CI=ON` a missing lock is an error. `EPM_<Name>_SOURCE` or `CPM_<Name>_SOURCE` uses a local
checkout with a warning; `EPM_CI=ON` makes that an error in locked mode.
`epm lock update [name...]`, `epm lock verify [--full]` (a locked package missing from the cache, as one only another platform asks for is, is fetched at its locked commit and checked) and `epm lock root`
work on the lock from the command line.

With epm loaded during `project()`, `find_package` never falls through to
CMake's own search. A name outside the graph is a package added: its source is
the lock's entry, else the one a dependency's `epm-lock.json` records, and
locked mode writes it into the lock (`EPM_CI=ON` stops instead, naming the
`find_package` line). When nothing records it the configure stops with the
line to add to the root `epm.cpp`. A package the machine provides is opted in
by name with `project.addSystem("Threads")`, and only then searched for as
CMake would; a `Find<Name>.cmake` on `CMAKE_MODULE_PATH` is the project's own
code and still runs.

The lock holds one version of each package for the whole tree. When projects
ask for different versions, the highest semantic version tag wins and anything
else stops the configure until the root's `epm.cpp` overrides it, as Go's
minimal version selection and `replace` do.

A binary cache entry's key includes the package's Merkle hash, and the entry
records the hashes of the packages its child build added, so a cached library
is only used with exactly the sources in the lock.

## Getting the epm binary

A project vendors one file, [`CMake/get_epm.cmake`](CMake/get_epm.cmake) (also
attached to every [release](https://github.com/eyalamirmusic/epm/releases)),
and includes it. By default it uses the **latest epm release**. It finds a
binary in this order, taking the first one whose version is in range:

1. `-DEPM_EXECUTABLE=/path/to/epm`, if given.
2. An `epm` installed on `PATH` (with its `share/epm/cmake/epm.cmake`):
   `-- epm: using epm 0.1.2 from /usr/local/bin/epm`.
3. A binary an earlier configure put in the cache, under `<cache>/tool/v<version>/`,
   unless the latest release is newer.
4. The latest release: its `manifest.json` is read from GitHub at most once a
   day (`-DEPM_UPDATE=ON` checks now), and the archive for this host (macOS,
   Linux x64/arm64, Windows x64/arm64) is downloaded, checked against the
   manifest's SHA-256 and unpacked into the cache. This takes a few seconds and
   prints `-- epm: downloaded epm 0.1.2 (macos-universal) into ...`.
5. Otherwise, or if the download fails: build that version from source on the
   host, once per machine, into the same place. This takes a minute or so and
   ignores the consumer's toolchain, so it also works when the consumer targets
   iOS or Android.

It then includes the `epm.cmake` that was installed with the chosen binary, so
`epm_add_package` and the binary always match. Offline, the cached binary is
used.

### Pinning a version

Set `EPM_VERSION` before the include (or pass `-DEPM_VERSION=...`), in CMake's
range syntax:

```cmake
set(EPM_VERSION "0.1...<0.2")    # any 0.1.x
include(cmake/get_epm.cmake)
```

`"0.1.2"` means 0.1.2 or newer (as in `find_package`), `"0.1...0.2"` includes
0.2.0. An `epm` on `PATH` or in the cache outside the range is skipped. When the
latest release is outside the range and the range starts at an exact `x.y.z`,
release `vx.y.z` is downloaded instead; no other releases are searched, so to
stay on an older line, start the range at a version that exists.

The cache is `~/Library/Caches/epm` on macOS, `$XDG_CACHE_HOME/epm` or
`~/.cache/epm` on Linux, `%LOCALAPPDATA%\epm` on Windows, or wherever `EPM_CACHE`
(CMake or environment variable) points. `-DEPM_BOOTSTRAP_NO_DOWNLOAD=ON` builds
from source instead of downloading, `EPM_SOURCE_REF` (or `EPM_SOURCE_URL`)
builds another tag, branch or commit from source, `EPM_SOURCE_DIR` (CMake or
environment variable) builds a local epm checkout incrementally at every
configure and uses it (for working on epm), and `EPM_VERBOSE=ON` shows what is tried. A `get_epm.cmake`
that is itself part of an epm checkout (the examples in this repository use
`../../CMake/get_epm.cmake`) builds that checkout unless `EPM_EXECUTABLE`,
`EPM_SOURCE_DIR`, `EPM_SOURCE_REF` or `EPM_SOURCE_URL` says otherwise; a vendored
copy never does.

## Building and installing epm

epm needs CMake 3.31 and a C++20 compiler. It builds itself with epm, as a
compiler builds with its previous release: `CMakeLists.txt` pins a released
epm (`EPM_VERSION`, `EPM_RELEASE_REPOSITORY`), `CMake/get_epm.cmake` downloads
it during `project()`, and it resolves eacp, Miro and NanoTest from `epm.cpp`
and the committed `epm-lock.json`; `find_package` reaches them through the
provider. `-DEPM_EXECUTABLE=<an epm>` uses another. Everything is linked
statically.

```bash
cmake -S . -B build && cmake --build build && cmake --install build --prefix ~/.local
```

This installs `bin/epm` and `share/epm/cmake/epm.cmake`, nothing else; with
`bin` on `PATH`, `get_epm.cmake` uses it. Tests run
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

Each job builds epm, runs `ctest`, then configures, builds and runs
`Examples/HelloEacp` with the binary it just built (`MODE auto`, so eacp is
fetched and built, Release only, into a cache inside the workspace), and
configures it again to check that eacp comes from the binary cache. The
package is uploaded as a workflow artifact.

`.github/workflows/release.yml` builds the same packages on pushes to `main` and
`release/**` and on `v*` tags (the list is at the top of the file):
`epm-<version>-<platform>.tar.gz` (`.zip` on Windows), holding `bin/epm` and
`share/epm/cmake/epm.cmake`, plus `SHA256SUMS`, `manifest.json` (what
`get_epm.cmake` reads) and a copy of `get_epm.cmake`. A branch push replaces
the assets of the rolling
[`nightly`](https://github.com/eyalamirmusic/epm/releases/tag/nightly)
prerelease (never "latest", so never picked by `get_epm.cmake`); a `v*` tag
publishes them on the [release](https://github.com/eyalamirmusic/epm/releases)
of that tag, which becomes the latest. Both workflows share their build matrix
through `.github/workflows/build.yml`.

### Releasing

Bump `project(epm VERSION X.Y.Z)` in `CMakeLists.txt`, merge to `main`, then
tag and push `vX.Y.Z`. That is all: no version or hash lives anywhere else in
the repository, and consumers pick the new release up within a day.

The Linux binaries are built on Ubuntu 24.04 and need glibc 2.38 or newer and
`libcurl.so.4`; the Windows binaries need the Visual C++ runtime.
