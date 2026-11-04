# epm

epm is a package manager for C++ projects that build with CMake. A project
declares its dependencies in a C++ recipe, `epm.cpp`, that also says how each
one is built (below), or with `epm_add_package(...)` calls in the same shape as
CPM.cmake's `CPMAddPackage`. A small native binary (`epm`) fetches each
dependency into a shared per-user cache. Where it can, it also builds the
dependency once per toolchain, in the configurations the consumer builds, and
hands the consumer imported targets that point at the cached libraries, so the
dependency is never `add_subdirectory`'d again. The target names are the same either way, so the
consumer's CMake does not change.

The longer-term design (remote binary caches, lockfiles, transitive
dependencies, a CPM compatibility shim) is in [PLAN.md](PLAN.md). What exists
today is described in [Docs/Graph.md](Docs/Graph.md) (the `epm.cpp` flow) and
[Docs/PoC.md](Docs/PoC.md) (the cache layout, the child builds, the bootstrap
and the `epm_add_package` flow).

## Status

This is a proof of concept. It is exercised with
[eacp](https://github.com/eyalamirmusic/eacp) as the test dependency, on macOS,
Windows and Linux in CI (see below). File formats, cache layout and the CMake
interface may all change.

## Quick start

```cmake
cmake_minimum_required(VERSION 3.31)
project(HelloEacp CXX)

set(EPM_CONFIGS Release) # only Release archives of eacp; Debug builds of hello link them on multi-config generators (missing configs map to Release)
include(cmake/get_epm.cmake)        # vendored from this repository's CMake/

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

The first configure builds epm itself (here from this checkout, see below) and
then builds eacp's Release configuration into the cache. Later configures, in any build directory
and any build type, reuse it and take about a second. Without the
`EPM_CONFIGS` line, epm builds the build type you configured instead, and a
configure in another build type adds just that configuration.

## Declaring dependencies in `epm.cpp`

The newer flow keeps CMake down to `find_package` and `target_link_libraries`
and moves the dependencies into a recipe, `epm.cpp`, beside the top-level
`CMakeLists.txt` (`Examples/HelloGraph`; the contract is
[Docs/Graph.md](Docs/Graph.md)):

```cmake
cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES cmake/get_epm.cmake)
project(HelloGraph CXX)

find_package(eacp REQUIRED)

add_executable(hello Main.cpp)
target_compile_features(hello PRIVATE cxx_std_20)
target_link_libraries(hello PRIVATE eacp-core)

enable_testing()
epm_add_recipe(HelloGraphRecipe)    # optional: epm.cpp as a target of this build
```

```cpp
#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
// How eacp is built into the cache: the default CMake flow, plus two options.
struct Eacp final : CMakePackage
{
    void configure(CMakeBuild& build) override
    {
        build.option("EACP_ENABLE_TESTS", "OFF");
        build.option("EACP_ENABLE_EXAMPLES", "OFF");
    }
};

struct HelloGraph final : Recipe
{
    void describe() override
    {
        auto eacp = add<Eacp>("eacp", "eyalamirmusic/eacp", "main");

        for (auto option: {"EACP_BUILD_GRAPHICS", "EACP_BUILD_WEBVIEW"})
            eacp.option(option, "OFF");

        eacp.config("Release");

        if (env().profile == "debug-deps")    // -DEPM_PROFILE=debug-deps
            eacp.config("Debug");
    }
};
} // namespace

EPM_RECIPE(HelloGraph)
```

A recipe is ordinary C++ that epm compiles into a small program and runs, in
two phases. `describe()` says what the project depends on, for the consumer's
toolchain and profile (`env()`): `add(name, git, tag)` with `.option()`,
`.config()`, `.mode()` and `.abiMatch()`. Then, for every package, epm runs
that package's build step, in dependency order, streaming its progress into the
configure output (for a package left to source the step builds nothing, but
its hooks still set the options its `add_subdirectory` gets): `add<T>` names the steps
(`T` derives from `CMakePackage`, whose `configure` and `afterBuild` hooks
adjust the default CMake build, or from `Package` for steps of its own), and a
plain `add` gets the default ones, which build any CMake project. Dependencies
with an `epm.cpp` of their own are walked the same way, and their own recipe
builds them. Recipes run on macOS and Linux; Windows has only
`epm_add_package` for now.

**Debugging.** With `epm_add_recipe`, the recipe is a target of your build.
Launch its Debug build under a debugger with `--env <build>/epm/env.json` to
stop in `describe()` (Xcode and Visual Studio get these arguments already; the
`HelloGraphRecipe-describe` target and the ctest run the same). Every
configure keeps each build step's input in `<build>/epm/steps/<name>.in.json`,
beside the command it ran (`<name>.command`, without the `--quiet` that keeps
a failure off stderr), so a step runs again by
hand: launch the target with `build --package eacp --in
<build>/epm/steps/eacp.in.json --out /tmp/eacp.out.json` and stop in the hooks.
An entry already cached is found rather than rebuilt, so that run takes a
hundredth of a second.

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

`epm cache ls` lists what the cache holds. Docs/PoC.md §9 has the details.
Builds cached by epm 0.1.2 and older use another layout and are ignored;
delete the cache's `bin/` directory once to reclaim the space.

## Getting the epm binary

A project vendors one file, [`CMake/get_epm.cmake`](CMake/get_epm.cmake) (also
attached to every [release](https://github.com/eyalamirmusic/epm/releases)),
and includes it. By default it uses the **newest epm release tag**, built from
source on your machine: it never downloads a binary, because an `epm.cpp`
recipe links epm's static libraries, which must come from your compiler. It
finds a binary in this order, taking the first one whose version is in range:

1. `-DEPM_EXECUTABLE=/path/to/epm`, if given.
2. An `epm` installed on `PATH` (with its `share/epm/cmake/epm.cmake`):
   `-- epm: using epm 0.1.2 from /usr/local/bin/epm`.
3. An epm an earlier configure built into the cache, under
   `<cache>/tool/v<version>/`, unless a newer release tag is in range.
4. The newest release tag `v<x.y.z>` in range, listed with `git ls-remote` at
   most once a day (`-DEPM_UPDATE=ON` lists now): shallow-fetched into the
   cache, then built and installed there, Debug and Release, once per machine.
   One status line per phase (`-- epm: epm v0.1.3: building Release`); the full
   log is `<cache>/tool/v<x.y.z>/epm-bootstrap.log`. The build ignores the
   consumer's toolchain file, build type and architectures, so it also works
   when the consumer targets iOS or Android. A cold configure of
   `Examples/HelloGraph` takes about 66 s on macOS: 41 s to build epm, Debug
   and Release, and 21 s to build eacp's Release, streamed as it goes; a warm
   reconfigure takes under a second.

It then includes the `epm.cmake` that was installed with the chosen binary, so
`epm_add_package` and the binary always match. Offline, the cached binary is
used; with nothing cached, the exact lower bound of `EPM_VERSION` (else `main`)
is built.

That cache entry is per tag, not per compiler: a second project on the same
machine that compiles with another compiler family stops at `epm_add_recipe`
(Docs/Graph.md §2) until the entry is deleted.

### Pinning a version

Set `EPM_VERSION` before the include (or pass `-DEPM_VERSION=...`), in CMake's
range syntax:

```cmake
set(EPM_VERSION "0.1...<0.2")    # any 0.1.x
include(cmake/get_epm.cmake)
```

`"0.1.2"` means 0.1.2 or newer (as in `find_package`), `"0.1...0.2"` includes
0.2.0. An `epm` on `PATH` or in the cache outside the range is skipped, and
the newest tag inside the range is built. When the tags list has none in range,
configure stops and names the releases there are.

The cache is `~/Library/Caches/epm` on macOS, `$XDG_CACHE_HOME/epm` or
`~/.cache/epm` on Linux, `%LOCALAPPDATA%\epm` on Windows, or wherever `EPM_CACHE`
(CMake or environment variable) points. `EPM_REPOSITORY` names another epm git
repository (a mirror or a fork), `EPM_SOURCE_REF` (or `EPM_SOURCE_URL`, a
tarball) builds another tag, branch or commit, `EPM_SOURCE_DIR` (CMake or
environment variable) builds a local epm checkout incrementally at every
configure and uses it (for working on epm; `EPM_BOOTSTRAP_SOURCE_DIR` is its
deprecated name), and `EPM_VERBOSE=ON` shows what is tried. A `get_epm.cmake`
that is itself part of an epm checkout (the examples in this repository use
`../../CMake/get_epm.cmake`) builds that checkout unless `EPM_EXECUTABLE`,
`EPM_SOURCE_DIR`, `EPM_SOURCE_REF` or `EPM_SOURCE_URL` says otherwise; a vendored
copy never does. Details are in Docs/PoC.md §8.

## Building and installing epm

epm needs CMake 3.31 and a C++20 compiler. Its own dependencies (eacp, Miro) are
fetched with CPM and linked statically.

```bash
cmake -S . -B build && cmake --build build && cmake --install build --prefix ~/.local
```

A single-config build is Release unless you say otherwise. This installs
`bin/epm`, the recipe libraries under `lib/Release/` with their headers under
`include/`, and `share/epm/cmake/epm.cmake` and `epm-provider.cmake`; with `bin`
on `PATH`, `get_epm.cmake` uses it. Install a Debug build into the same prefix
too (`cmake --install build-debug --config Debug --prefix ~/.local`) to give
Debug builds of `epm_add_recipe` targets `lib/Debug/`; the Release `bin/epm`
stays. Docs/PoC.md §8 lists the install. Tests run
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
`Examples/HelloEacp` through `get_epm.cmake`: (a) with the binary it just built
(`MODE auto`, so eacp is fetched and built, Release only, into a cache inside
the workspace); then, from a vendored copy of the example, (b) with the epm
0.1.1 release unpacked onto `PATH`, which must be used. On the macOS job only,
(c) with nothing set and no epm on `PATH`, which must build the newest release
tag from source, Debug and Release, and (d) with 0.1.1 on `PATH` again and
`EPM_VERSION=0.1.2`, which must skip it and use the entry (c) built, building
nothing. The package (Debug and Release installed into one prefix) is uploaded
as a workflow artifact.

`.github/workflows/release.yml` builds the same packages on pushes to `main` and
`release/**` and on `v*` tags (the list is at the top of the file):
`epm-<version>-<platform>.tar.gz` (`.zip` on Windows), an installed epm
(`bin/`, `lib/Debug/`, `lib/Release/`, `include/`, `share/`) to unpack into a
prefix and put on `PATH`, plus `SHA256SUMS` and a copy of `get_epm.cmake`.
`get_epm.cmake` downloads none of them. A branch push replaces the assets of
the rolling [`nightly`](https://github.com/eyalamirmusic/epm/releases/tag/nightly)
prerelease; a `v*` tag publishes them on the
[release](https://github.com/eyalamirmusic/epm/releases) of that tag. Both
workflows share their build matrix through `.github/workflows/build.yml`.

### Releasing

Bump `project(epm VERSION X.Y.Z)` in `CMakeLists.txt`, merge to `main`, then
tag and push `vX.Y.Z`. That is all: no version or hash lives anywhere else in
the repository, and consumers build the new tag within a day.

The Linux binaries are built on Ubuntu 24.04 and need glibc 2.38 or newer and
`libcurl.so.4`; the Windows binaries need the Visual C++ runtime. An archive's
epm compiles recipes with the compiler CI built it with, at CI's path, and its
libraries come from that compiler: where that path does not exist, it cannot
run an `epm.cpp`, so a project with a manifest is better served by
`get_epm.cmake`'s own build.
