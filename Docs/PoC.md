# epm proof of concept — scope and contract

> The per-call `epm_add_package` flow of §1–§3 is superseded by `Docs/Graph.md` (an
> `epm.cpp` manifest, CMake's dependency provider, `epm configure`) and kept for
> migration. The cache, fingerprint, config mapping and child build described here
> still hold except where `Docs/Graph.md` says otherwise.

Goal: a consumer project that depends on eacp, where epm keeps a **local** cache of eacp's
static libraries (the configurations the consumer builds, §9), and the consumer links
against those libraries directly when they exist. Nothing online (no remote stores, no
lockfile, no signing) in this stage.

Everything here is PoC-level and may be replaced; the shape follows `PLAN.md`.

## 1. User-facing flow

```cmake
cmake_minimum_required(VERSION 3.31)
project(HelloEacp CXX)
set(EPM_CONFIGS Release) # only Release archives of eacp; Debug builds of hello link them on multi-config generators (missing configs map to Release)
include(cmake/get_epm.cmake)                 # finds or builds epm, includes its module (§8)

epm_add_package(
  NAME eacp
  GITHUB_REPOSITORY eyalamirmusic/eacp
  GIT_TAG main                                # resolved to a commit by epm
  MODE auto                                   # auto | prebuilt | source
  OPTIONS "EACP_BUILD_GRAPHICS OFF" "EACP_BUILD_WEBVIEW OFF"
          "EACP_ENABLE_TESTS OFF" "EACP_ENABLE_EXAMPLES OFF")

add_executable(hello Main.cpp)
target_link_libraries(hello PRIVATE eacp-core)   # same target name in both modes
```

- `MODE source`: fetch into the source cache and `add_subdirectory` (CPM behaviour).
- `MODE prebuilt`: require a binary cache entry; build one if missing; never add_subdirectory.
- `MODE auto`: use the binary cache entry if it holds every config the consumer needs;
  otherwise build the missing ones (only those, §9), then use it.
  (`EPM_BUILD_MISSING_BINARIES=OFF` makes `auto` fall back to source instead.)

Proof: configure the example twice in fresh build dirs. First run builds eacp into the cache
(slow). Second run is seconds, `add_subdirectory` never happens, and the linked archives live
under `~/.cache/epm/bin/eacp/<platform>/<fp12>/lib/<Config>/`. `MODE source` still works.

## 2. Files exchanged between `epm.cmake` and the `epm` binary

All under `${CMAKE_BINARY_DIR}/epm/`.

### `env.json` (written once per configure by `epm.cmake`)

```json
{ "schema": 1,
  "cmake": "/path/to/cmake", "cmakeVersion": "3.31.6", "generator": "Ninja",
  "makeProgram": "/opt/homebrew/bin/ninja",
  "isMultiConfig": false,
  "buildType": "Debug", "configurationTypes": [],
  "cCompiler": "/usr/bin/cc", "cxxCompiler": "/usr/bin/c++",
  "cxxCompilerId": "AppleClang", "cxxCompilerVersion": "17.0.0.17000013",
  "systemName": "Darwin", "systemProcessor": "arm64", "crossCompiling": false,
  "osxArchitectures": "", "osxDeploymentTarget": "11.0",
  "osxSysroot": "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
  "androidAbi": "",
  "cxxStandard": "20",
  "toolchainFile": "",
  "profile": "",
  "epmExecutable": "/path/to/epm" }
```

`osxSysroot` is `CMAKE_OSX_SYSROOT` (an SDK name such as `iphonesimulator` under Xcode,
the resolved path under other generators) and `androidAbi` is `CMAKE_ANDROID_ARCH_ABI`
(else the NDK toolchain's `ANDROID_ABI`); both feed the platform name (§9), and a
cross-compiling child build gets them (§5). `crossCompiling` is `CMAKE_CROSSCOMPILING`:
the consumer named its target system itself. For an Apple device or simulator SDK (iOS,
tvOS, watchOS, visionOS), whose `CMAKE_SYSTEM_PROCESSOR` CMake leaves empty, an empty
`CMAKE_OSX_ARCHITECTURES` is written as what the SDK builds for by default: Xcode's
`ARCHS_STANDARD` (`CMAKE_XCODE_ARCHS`, `arm64;x86_64` for a simulator), else the
compiler's default target (`CMAKE_CXX_COMPILER_ARCHITECTURE_ID`), so the platform name and
the fingerprint carry the architecture and child builds build exactly it; and an empty
`CMAKE_OSX_DEPLOYMENT_TARGET` as `CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET` (`TVOS_`,
`WATCHOS_`, `XROS_` alike) when the project sets that instead (`epm_osx_architectures`,
`epm_osx_deployment_target`).
Empty string / empty array when a variable is unset. Written at the first `epm_add_package`
call (not at include time), once per configure. `cCompiler` is empty when the consumer does
not enable C; the binary must not pass an empty `-DCMAKE_C_COMPILER` to child builds.
`makeProgram` (`CMAKE_MAKE_PROGRAM`) is passed to a child build that reuses the consumer's
generator, so a build tool that is not on PATH (CLion's bundled ninja) is still found.
`epmExecutable` is `EPM_EXECUTABLE`, the epm binary running the configure: the recipe SDK
is found from its install prefix (`RecipeSdk`, §8), and a child build with dependencies
gets it as its `EPM_EXECUTABLE`. Like `profile`, it is in no fingerprint.

### `requests/<name>.json` (one per `epm_add_package` call)

```json
{ "schema": 2,
  "name": "eacp",
  "gitUrl": "https://github.com/eyalamirmusic/eacp.git",
  "gitRef": "main",
  "mode": "auto",
  "options": { "EACP_BUILD_GRAPHICS": "OFF", "EACP_ENABLE_TESTS": "OFF" },
  "buildMissingBinaries": true,
  "configs": ["Debug"],
  "abiMatch": "compatible" }
```

`configs` is `EPM_CONFIGS` as `epm.cmake` resolved it (it knows the generator): the
configs the consumer needs, each `Debug` or `Release`. `abiMatch` is `EPM_ABI_MATCH`
(`compatible`, `strict` or `any`). Both are explained in §9. The binary reads schema 2
only and refuses any other with "the request for eacp has schema 1, but this epm reads
schema 2: epm.cmake and the epm binary come from different versions …"; it also refuses
an empty `configs`, a config other than Debug, Release, RelWithDebInfo or MinSizeRel (the
last two are built as Release) and an unknown `abiMatch`.

Invocation: `epm resolve --request <json> --env <json> --response <cmake> [--cache <dir>]
[--verbose]`. `--cache` is passed only when the CMake variable `EPM_CACHE` is set;
`EPM_VERBOSE` adds `--verbose` and streams the binary's output (step lines plus the child
builds' output, live). Without it the binary's stdout carries only one
`-- epm: building <name> from source into the cache (<Config>, <platform>), this can take
a while` line before each config it builds and `-- epm: built <name> (<Config>) in 1m 42s`
after; `epm.cmake` passes stdout through as it is written and captures only stderr. Non-zero exit with a one-line message on stderr means configure
fails; when a child build failed, the last 40 lines of its output come first and the full
log stays in the kept build dir (`epm-configure.log` / `epm-build.log`). `EPM_BUILD_MISSING_BINARIES` is a global `option()`
(default ON) whose value lands in `buildMissingBinaries`. `GIT_TAG` is required; `NAME`
defaults to the repository name as in CPM.

### `responses/<name>.cmake` (written by the binary, included by `epm.cmake`)

```cmake
set(epm_eacp_MODE "prebuilt")                                   # or "source"
set(epm_eacp_SOURCE_DIR "/Users/me/.cache/epm/src/eacp/3f1c0a9b2d4e")
# prebuilt only:
set(epm_eacp_PREFIX "/Users/me/.cache/epm/bin/eacp/macos-arm64/9a8b7c6d5e4f")
set(epm_eacp_TARGETS_FILE "${epm_eacp_PREFIX}/epm-targets.cmake")
set(epm_eacp_PLATFORM "macos-arm64")
set(epm_eacp_CONFIGS "Debug;Release")                           # what the entry holds
set(epm_eacp_NOTE "built with AppleClang 17.0.0.17000013, current 17.0.0.17000319")
```

`NOTE` is there only when the entry's fingerprint is not identical to the current one (a
compatible match, §9). `epm.cmake` then either `include()`s the targets file (prebuilt) or
sets the OPTIONS as plain variables and calls
`add_subdirectory(<SOURCE_DIR> ${CMAKE_BINARY_DIR}/_deps/<name>-build)` (source). In both
cases it sets `<name>_SOURCE_DIR`, `<name>_ADDED` and `<name>_MODE` in the caller's scope,
and prints one line: `-- epm: eacp (prebuilt, macos-arm64, 9a8b7c6d5e4f)`, with the note
appended when there is one (`-- epm: eacp (prebuilt, macos-arm64, 9a8b7c6d5e4f, built
with AppleClang 17.0.0.17000013, current 17.0.0.17000319)`), or
`-- epm: eacp (source, 3f1c0a9b2d4e)`.

## 3. Cache layout (PoC subset of PLAN.md §5.3)

```
$EPM_CACHE  (default: <platform cache dir>/epm, e.g. ~/Library/Caches/epm or ~/.cache/epm)
  src/<name>/<sourceId12>/          clean snapshot, no .git
  src/<name>/<sourceId12>.json      completeness marker: {url, commit, fetchedAt}
  bin/<name>/<platform>/<fp12>/     one entry per ABI fingerprint (§9)
      manifest.json                 {schema, fingerprint, sourceId, createdAt}; written first
      <Config>.json                 per-config completeness marker, written last for that
                                    config: {config, builtAt, compilerPath, compilerVersion}
      lib/<Config>/*.a|*.lib
      export-<Config>.cmake (+ .names, .aliases)   CMake export() output, namespaced epm_<config>::
      gen/<Config>/…                relocated per-config build-tree paths (include dirs, objects)
      gen/shared/<sha12>/<relpath>  relocated INTERFACE_SOURCES files, content-addressed
      epm-targets.cmake             facade targets (§5), regenerated when a config is added
  build/<name>/<platform>/<fp12>/<Config>/   child build trees; deleted after the harvest
  cpm/                              CPM_SOURCE_CACHE for child builds (nested CPM deps)
  recipe/<key12>/epm-recipe         a compiled epm.cpp, with compile.log (Graph.md §2)
  tool/<key>/                       a bootstrapped epm (§8), an epm install prefix:
      bin/epm                       the binary
      lib/<Config>/*.a              its recipe libraries, Debug and Release
      include/                      their headers
      share/epm/cmake/epm.cmake     the module that goes with it (+ epm-provider.cmake)
      share/epm/epm-config          one line: the configuration of bin/epm (Release wins)
      epm-bootstrap.log             the log of its fetch and builds
      src/                          the fetched source (tag entries v<x.y.z>, <ref>)
      build-<generator>[-debug]/, cpm/   build trees and CPM downloads; deleted once a
                                    tag entry is built, kept by a local-<hash> entry
  tool/<key>.json                   its completeness marker: {ref, origin, url, version, builtAt}
  tool/<key>.lock                   CMake file(LOCK) held while bootstrapping
  tool/latest.json                  the epm repository's release tags, plus checkedAt
  tmp/                              staging; final locations are reached by rename
```

The build steps' files of `epm configure` (`<name>.in.json`, `<name>.out.json`,
`<name>.command`) are not in the cache: they live in the consumer's build directory, under
`<build>/epm/steps/` (`Docs/Graph.md` §6).

Locks: one eacp `IPC::Lock` per package name, held while fetching a snapshot and while
building an entry (waits up to an hour for another process); the build step that builds
the entry takes it itself (`Package::cached`, §5), and the configure, which runs the
step, in its own process or another, holds none meanwhile. A recipe's compile holds `recipe-<key12>` the same way. `IPC::Lock` keeps its lock
files in its own per-user directory (`<appData>/eacp/Locks`) and takes no path, so the lock
is named `epm-<sha256(cache root)[:12]>-<name>` to keep separate caches independent; there
is no `locks/` directory. The cache root is resolved (`weakly_canonical`) before use so
every path epm writes and matches has one spelling.

`sourceId = sha256("git\n" + url + "\n" + commit)`, first 12 hex chars as directory name.
`fp12` is the first 12 hex chars of `sha256(canonical fingerprint JSON)`. The fingerprint
(`AbiFingerprint`, recorded in `manifest.json`) holds `schema` (2), `platform`,
`compilerId`, `compilerVersion` (full), `compilerMajor`, `compilerPath`, `systemName`,
`systemProcessor`, `osxArchitectures`, `osxDeploymentTarget`, `cxxStandard`,
`toolchainFile` and `toolchainFileHash` (sha256 of its contents, when there is one),
`options` (sorted) and `sourceId`. The canonical form is Miro's compact output of the
struct (`Miro::toJSONString`): Miro objects are `std::map`s, so every key, options
included, comes out sorted, with no whitespace. The configs are *not* in it: one entry
holds every config built for that fingerprint, added one at a time. Which entry a request
may use is decided by `EPM_ABI_MATCH` (§9), so an Xcode consumer can use the entry a Ninja
consumer built with `/usr/bin/c++`. Full toolchain probing (compiler content hash, macros)
comes later.

Entries of the 0.1.2 layout (`bin/<name>/<key12>/` with `<key12>.json` beside it) are
ignored, never read or deleted; `epm cache ls` reports them. Delete `bin/` once after
upgrading (it only holds rebuildable binaries).

`epm cache ls [--cache <dir>]` prints one line per binary entry
(`<name>  <platform>  <configs>  <compiler> <major>  <size>  <fp12>`), then the sources and
the tools; `epm cache path` prints the cache root.

## 4. Source fetch

1. Resolve `gitRef` to a commit: 40 hex chars → as is; otherwise `git ls-remote <url>` and
   pick, in order, `refs/tags/<ref>^{}`, `refs/tags/<ref>`, `refs/heads/<ref>`, then an
   exact ref name (`HEAD`, `refs/...`).
2. Under the per-name lock, if the marker exists → done. Else into `tmp/<random>/`:
   `git init`, `git remote add origin <url>`, `git fetch --depth 1 origin <commit>`,
   `git checkout --detach FETCH_HEAD`, `git submodule update --init --recursive --depth 1`
   (if any), delete `.git` (and submodules' `.git` files), rename into place, write the
   marker. A server that refuses the shallow fetch by commit gets `git fetch origin <ref>`
   (every branch and tag for a bare hash) and `git checkout --detach <commit>`.
3. Shell out to the `git` CLI through `eacp::Processes` with a non-interactive environment
   (`GIT_TERMINAL_PROMPT=0`). Lift the wrapper from
   `/Users/eyalamir/Code/AgentWorkspace/Lib/WorkspaceCore/Git.{h,cpp}`.

## 5. Binary build and harvest

A package's entry is built by its build step (`Docs/Graph.md` §2): `Package::cached` and
`CMakeBuild` (`Recipe/`). Under `epm configure` they run in the recipe executable that
owns the step: the root's, which runs the configure itself, in its own process, any
other recipe's as a process of its own (`Docs/Graph.md` §6). They run in the epm
process for `epm resolve` (`epm_add_package`), and in the configure's process for an
owner without a recipe file and wherever there are no recipe executables (there, epm's
own). The default steps do what follows; custom steps may change the options,
definitions, configs and generator, or add files after the build, through the same two.

`cached` decides from the request's mode whether anything is built (`Spec/Request.h`,
`decide`; §9 for picking). If so, under the package lock it picks the entry again (another
process may have finished it) or creates it (`manifest.json` first), and clears whatever
each missing config, one the entry lacks a marker for, left in `lib/`, `gen/` or its export
files. Then `CMakeBuild::run` configures, builds and harvests each of those configs, and
only those, in turn (child build dir `build/<name>/<platform>/<fp12>/<Config>`, made fresh):

```
cmake -S <src> -B <builddir> -G <generator>
      -DCMAKE_BUILD_TYPE=<Config>
      -DCMAKE_TOOLCHAIN_FILE=…                       # the consumer's, if any
      -DCMAKE_SYSTEM_NAME=… -DCMAKE_SYSTEM_PROCESSOR=… -DCMAKE_OSX_SYSROOT=…
                                                     # cross-compiling without a toolchain file
      -DCMAKE_C_COMPILER=… -DCMAKE_CXX_COMPILER=…
      -DCMAKE_OSX_ARCHITECTURES=… -DCMAKE_OSX_DEPLOYMENT_TARGET=… -DCMAKE_CXX_STANDARD=…
      -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=<prefix>/lib/<Config>
      -DCPM_SOURCE_CACHE=<cache>/cpm
      -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<builddir>/epm-export.cmake
      -DEPM_EXPORT_FILE=<prefix>/export-<Config>.cmake
      -DEPM_EXPORT_NAMESPACE=epm_<config>::          # epm_debug:: / epm_release::
      -D<OPTION>=<VALUE> …                           # the request's, as CMakeBuild::option left them
      -D<NAME>=<VALUE> …                             # CMakeBuild::define, last
cmake --build <builddir> --parallel
```

A package with dependencies also gets the nested flags of `Docs/Graph.md` §10. The options
and definitions are part of the fingerprint (`Package::fingerprint(source, build)`), so
the entry is keyed by what was actually passed.

- Generator: the consumer's generator (and `CMAKE_MAKE_PROGRAM`) if it is single-config;
  otherwise `Ninja` if found on PATH (passed as `CMAKE_MAKE_PROGRAM`), else
  `Unix Makefiles` (or the platform default on Windows); `CMakeBuild::generator` overrides
  it. `-D` flags whose env value is empty are left out; `-DCMAKE_TOOLCHAIN_FILE` is passed
  when the consumer has one.
- Target system: when the consumer cross-compiles without a toolchain file
  (`crossCompilesWithoutToolchain`, `Spec/Platform`: `env.json`'s `crossCompiling` set
  and its `toolchainFile` empty), as with `-DCMAKE_SYSTEM_NAME=iOS
  -DCMAKE_OSX_SYSROOT=iphonesimulator`, the child gets `CMAKE_SYSTEM_NAME`,
  `CMAKE_SYSTEM_PROCESSOR` and `CMAKE_OSX_SYSROOT` (each when non-empty; CMake leaves the
  processor empty for iOS), and for Android `CMAKE_ANDROID_ARCH_ABI`, so it configures for
  that system before its `project()` instead of for the host; with the architectures and
  deployment target above, an iOS simulator consumer's packages are simulator archives
  (`Tests/GraphIos.cmake`). A toolchain file, which the child gets too, names the system
  itself, and a native consumer gets none of these. Not forwarded: `CMAKE_ANDROID_NDK`
  and `CMAKE_SYSTEM_VERSION` (an Android child without a toolchain file finds the NDK
  from the environment), nor the NDK toolchain's `ANDROID_ABI`/`ANDROID_PLATFORM`.
- `epm-export.cmake` (source of truth: `CMake/epm-export.cmake` in this repo, embedded into
  the binary at build time and written into `<builddir>` before configure) does:
  `cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL epm_export_all_targets)`.
  That function walks every directory (`SUBDIRECTORIES` property, recursively), collects
  `BUILDSYSTEM_TARGETS` whose TYPE is `STATIC_LIBRARY`, `OBJECT_LIBRARY`, `INTERFACE_LIBRARY`,
  `SHARED_LIBRARY` or `MODULE_LIBRARY`, and calls
  `export(TARGETS … NAMESPACE ${EPM_EXPORT_NAMESPACE} FILE ${EPM_EXPORT_FILE})`.
- The script schedules itself a second time on first run so it executes after any DEFER
  calls the dependency itself makes (eacp's plugin and Android modules use DEFER). Besides
  the export file it writes `${EPM_EXPORT_FILE}.names`, one exported target name per line,
  which the harvester reads instead of scraping the export file, and
  `${EPM_EXPORT_FILE}.aliases`, one `<alias> <target>` line per ALIAS of an exported
  target (`Docs/Graph.md` §9).
- Because archives are written straight into the prefix, the export file already points at
  cached `.a` files. Include directories point into the source cache (stable) and into
  `<cache>/cpm` for nested CPM dependencies (stable).
- **Relocation of build-tree paths.** Verified against eacp: the export file still references
  the child build tree in a few places — ResEmbed-generated include dirs and registration
  `.cpp` files in `INTERFACE_SOURCES` (Miro, eacp-webview-bridge), and `IMPORTED_OBJECTS` of
  OBJECT libraries (MiroFormats, MiroCodegenWriter). The harvester therefore, per config:
  first, every build-tree file listed in an `INTERFACE_SOURCES` property is copied to
  `<prefix>/gen/shared/<sha256(contents)[:12]>/<path relative to builddir>` (unless already
  there) and that whole path is replaced in the export file; then it scans the export file
  for every remaining path that starts with `<builddir>`, copies each referenced file or
  directory to `<prefix>/gen/<Config>/<path relative to builddir>`, and replaces the
  literal `<builddir>` prefix with `<prefix>/gen/<Config>` throughout the export file. After
  that no path references the build tree and the build dir is deleted. If any reference
  survives the rewrite (unexpected), warn and keep the build dir.
- **Why `gen/shared` is content-addressed.** Xcode fails a target whose sources vary by
  configuration, and the registration `.cpp` files in `INTERFACE_SOURCES` would otherwise
  be one path per config. Because the path is derived from the file's contents, every
  config that generates the same file names the same path, including a config added to
  the entry weeks later, without touching the export files of the configs already there.
  For eacp this shares the two `*_Register.cpp` files; the `.o` files and the generated
  include dirs (whose `.d` files embed the build path) stay per config. `gen/shared` is
  never cleared.
- `CMakeBuild::harvest` marks nothing. Once `cached`'s callback has returned (`run()`,
  then the `afterBuild` hook), `cached` adds each config the callback left harvested (an
  export file with its `.names`) to the entry: it writes `<Config>.json` (the config is
  now in the entry) and regenerates `epm-targets.cmake` for the configs whose markers
  exist (`addConfigToEntry`); a config left without an export file fails the step. A
  callback that throws adds none, so a first build that fails leaves an entry holding
  only `manifest.json`, which the next run picks (§9) and builds into. If any export file
  mentions `Threads::Threads`, the facade adds `find_package(Threads REQUIRED)` before the
  includes:

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/export-Debug.cmake")      # one per config present
include("${CMAKE_CURRENT_LIST_DIR}/export-Release.cmake")
foreach(name IN ITEMS eacp-core eacp-network …)           # every exported library
  if(NOT TARGET ${name})
    add_library(${name} INTERFACE IMPORTED GLOBAL)
    target_link_libraries(${name} INTERFACE
      "$<IF:$<CONFIG:Debug>,epm_debug::${name},epm_release::${name}>")
  endif()
endforeach()
if(NOT TARGET fmt::fmt)                                     # one per line of .aliases
  add_library(fmt::fmt ALIAS fmt)
endif()
```

  The selector maps each consumer config to the same-named config if the entry has it,
  else Release if present, else the first available, as nested `$<IF:$<CONFIG:…>,…,…>`
  expressions: `epm_release::${name}` alone for {Release}; for {Debug, Release,
  RelWithDebInfo}
  `$<IF:$<CONFIG:Debug>,epm_debug::…,$<IF:$<CONFIG:RelWithDebInfo>,epm_relwithdebinfo::…,epm_release::…>>`.
  So with {Release} only, a Debug consumer links the Release archives. The list of exported
  names is the union of the `.names` files. Known PoC limitation: the facade covers every
  exported name (including nested deps such as `Miro`, `fmt`); if the consumer defines one
  of those itself the guard keeps the consumer's target and the program may end up with
  two copies. To be addressed after the PoC.
- The marker is written before the facade, so the facade never names a config whose
  files may still be cleared. A failure in any config of a run (or in `afterBuild`)
  marks none of that run's configs, the ones harvested before it included: the next run
  clears and builds them all again.

## 6. Repository layout (mirrors eacp / AgentWorkspace)

```
epm/
  CMakeLists.txt            CMake 3.31; options EPM_ENABLE_TESTS (default ON when top-level);
                            install: bin/epm, lib/<Config>/, include/,
                            share/epm/cmake/{epm,epm-provider}.cmake,
                            share/epm/epm-config (§8)
  README.md
  .clang-format .gitignore  copied from /Users/eyalamir/Code/eacp
  CLAUDE.md                 conventions pointer (eacp style, NanoTest, Miro for JSON)
  CMake/CPM.cmake           vendored 0.40.2 (same as other projects), for epm's own deps
  CMake/Findeacp.cmake, FindMiro.cmake, FindNanoTest.cmake
  CMake/get_epm.cmake       the file users vendor and include: finds the binary (§8)
  CMake/epm.cmake           consumer module (epm_add_package), installed with the binary
  CMake/epm-export.cmake    injected into child builds (see §5)
  Lib/epm/                  the static library `epm-recipe`, namespace `epm::`: the recipe
                            SDK, build steps included, and the driver (the graph walk,
                            compiling and running recipes, Recipe/RecipeConfigure), which
                            epm and every recipe executable link; CLAUDE.md has the
                            current layout
    Util/Sha256.{h,cpp}     lifted from /Users/eyalamir/Code/PackagesCPP/Lib/pkgcpp/Sha256.*
    Util/Git.{h,cpp}        lifted from AgentWorkspace
    Util/Command, Log, Text, Timestamp   child processes, findExecutable, --verbose, text
    Cache/CacheRoot, SourceCache, BinaryCache, PackageLock
    Cache/Fingerprint       AbiFingerprint, EPM_ABI_MATCH policies, the configure note
    Cache/CacheListing      `epm cache ls`
    Spec/Request, Env       Miro-reflected structs for the JSON files; Request's
                            `decide` is the mode decision
    Spec/Platform           the platform name (§9)
    Build/ChildBuild, Harvest   (+ EmbeddedScripts.cpp.in: epm-export.cmake compiled in)
    Build/Configs           config validation, mapping and the facade's selector
    Emit/Response
    Resolve/Resolve         the `epm resolve` pipeline (the default build steps, in-process)
  Apps/epm/Version.h.in     generated Version.h: epm::version from project(VERSION)
  Apps/epm/Main.cpp         Apps::runConsole + manual subcommand dispatch (configure,
                            graph-cmake, describe, recipe-sdk, resolve, cache path,
                            cache ls, --version); configure compiles the root epm.cpp
                            and runs its executable's configure (Docs/Graph.md §6)
  Tests/                    NanoTest: Sha256 vectors, fingerprint canonicalisation and
                            matching, platform names, config mapping and selectors, entry
                            picking, response text, ref resolution, export-file
                            scanning/relocation, .names parsing, generator choice, child
                            configure arguments, mode decision, request schema; plus
                            GetEpmRanges.cmake and EpmConfigsAuto.cmake (cmake -P)
  Examples/HelloEacp/       the consumer project from §1
```

eacp is consumed with `EACP_BUILD_GRAPHICS OFF`, `EACP_BUILD_WEBVIEW OFF`,
`EACP_ENABLE_TESTS OFF`, `EACP_ENABLE_EXAMPLES OFF`; link `eacp-core` only.

## 7. Out of scope for the PoC

Remote stores, lockfile and prefetch, CPM shim for transitive deps, URL archives, patches,
case-insensitive name matching, compiler content hashing, Windows/Linux validation (code
should stay portable, but only macOS is exercised).

## 8. Bootstrap

No epm version or hash is written into the repository, and CI never commits to it.
Consumers vendor `CMake/get_epm.cmake` (also attached verbatim to every release). It finds
an epm binary and then `include()`s the `epm.cmake` installed with *that* binary, so the
module defining `epm_add_package` always matches the binary it talks to. `epm.cmake` on
its own does nothing but stop with "include get_epm.cmake instead" when `EPM_EXECUTABLE`
is unset.

There is one version: `project(epm VERSION …)`. `epm --version` prints it (through
`Apps/epm/Version.h.in`), release.yml names the assets after it and checks it against the
`v*` tag. Releasing is: bump `project(VERSION)`, merge to main, tag `vX.Y.Z`. Nothing is
edited after the release.

get_epm never downloads an epm binary. A recipe (`Docs/Graph.md` §2) links epm's static
libraries, which must come from the compiler on the machine that uses them, so an epm
that is not installed already is built from source there: Debug and Release, once per
version (a tag), or incrementally at every configure for a local checkout. Measured on
an M-series Mac: a cold configure of `Examples/HelloGraph` (Debug and Release of epm
and eacp) takes 36 s of wall time, a reconfigure under 1 s.

### Version range

`EPM_VERSION`, set before the include (or `-D`), takes CMake's range syntax:
`"0.1.2"` means `>= 0.1.2` (as in `find_package`), `"0.1...<0.2"` excludes the maximum,
`"0.1...0.2"` includes it (compared component-wise, so `0.2.1` is outside), unset accepts
any version. Only plain numeric versions match. `epm_version_in_range(version range out)`
implements it; `Tests/GetEpmRanges.cmake` tests it (`ctest -R get_epm_ranges`).

### Resolution order

First in-range match wins:

1. `EPM_EXECUTABLE`, when set by the user: its `--version` must be in range, and a module
   must be found: `<dir of binary>/../share/epm/cmake/epm.cmake`, else the `epm.cmake` beside
   get_epm.cmake (true inside this repository, where `build/Apps/epm/epm` has no installed
   module). Otherwise a warning, and the search goes on.
   - Then `EPM_SOURCE_DIR` (CMake variable, else environment; relative to the top-level
     source directory), when set, skipping 2–4: an epm checkout (a directory whose
     `CMakeLists.txt` has `project(epm`, else configure stops), built and installed into
     `<cache>/tool/local-<sha256(real path)[:12]>` on every configure, under that
     entry's lock, as *Building an entry* below describes. Its build directories stay
     in the entry, so after the first build each configure is an incremental
     `cmake --build` plus `cmake --install` of Debug and Release. The entry's `bin/epm`
     and `share/epm/cmake/epm.cmake` are used and the marker rewritten; the version is
     printed (`-- epm: using epm 0.1.3 built from …`), never checked against
     `EPM_VERSION`. For testing an unreleased epm from a consumer.
     `EPM_BOOTSTRAP_SOURCE_DIR`, the old name, is read as `EPM_SOURCE_DIR` when neither
     the variable nor the environment variable is set, with
     `-- epm: EPM_BOOTSTRAP_SOURCE_DIR is deprecated; using it as EPM_SOURCE_DIR`.
   - Else, when get_epm.cmake is itself `<dir>/CMake/get_epm.cmake` of an epm checkout
     (`<dir>/CMakeLists.txt` has `project(epm`, the same check) and no user
     `EPM_EXECUTABLE`, `EPM_SOURCE_REF` or `EPM_SOURCE_URL` is given, `EPM_SOURCE_DIR`
     defaults to `<dir>`: `-- epm: get_epm.cmake is inside the epm checkout <dir>;
     building epm from it (-DEPM_EXECUTABLE or -DEPM_SOURCE_DIR to use another)`. That is
     how the examples in this repository, which list `../../CMake/get_epm.cmake`, use
     the epm they sit in; a copy vendored into a consumer never matches.
     `Tests/GetEpmSourceDir.cmake` (`ctest -R get_epm_source_dir`) tests the checks,
     where the setting comes from and the entry naming.
2. `epm` on `PATH` (`find_program(… NO_CACHE)`, PATH only, never re-rooted by a cross
   toolchain), when in range and `<dir of binary, symlinks resolved>/../share/epm/cmake/epm.cmake`
   exists; otherwise skipped with a verbose note. `-- epm: using epm 0.1.2 from /usr/local/bin/epm`.
3. The newest in-range entry `<cache>/tool/v*/` with its marker, `bin/epm` and
   `share/epm/cmake/epm.cmake`, *unless* the newest release tag in range (step 4's
   listing) is newer: without that exception a machine would stay on the first version
   it ever built. Reading the listing here costs no network within the 24 hours below.
4. A source build of a release tag. The release versions are the `refs/tags/v<x.y.z>`
   of `git ls-remote --tags --refs <repository>` (numeric versions only: no `nightly`,
   no prerelease spellings), where the repository is `EPM_REPOSITORY`, by default
   `https://github.com/eyalamirmusic/epm`. The listing is kept in
   `<cache>/tool/latest.json` (`{schema: 2, checkedAt: <unix seconds>, repository,
   versions}`) and redone only when it is older than 24 hours, lists another
   repository, or `EPM_UPDATE` is ON (a one-off: get_epm removes it from the cache after
   the configure); if listing fails (offline, no git) a stale copy is used. A copy that
   is not in that form (another schema, a `checkedAt` that is not unix seconds,
   `versions` not a list) counts as none; `Tests/GetEpmLatest.cmake`
   (`ctest -R get_epm_latest`) tests the reading, offline. The tag
   built is `v<newest listed version in range>`. With versions listed but none in range
   of a set `EPM_VERSION`, configure stops, naming the releases. With no listing at all,
   it is `v<exact lower bound x.y.z>` of `EPM_VERSION`, else `main`.
   The source is fetched into `<cache>/tool/<tag>/src`: with git, `git init`,
   `git fetch --depth 1 <repository> <tag>` and `git checkout --detach FETCH_HEAD`
   (`GIT_TERMINAL_PROMPT=0`; `-- epm: fetching epm v0.1.3 from <repository>`); without
   git, and only for a `https://github.com/` repository, GitHub's tarball
   `<repository>/archive/<tag>.tar.gz`, downloaded and extracted through
   `<cache>/tmp/`. It is then built and installed into `<cache>/tool/<tag>/` (*Building an
   entry*), `bin/epm --version` must run, the build directories are pruned, and the
   marker is written last. `-- epm: built epm 0.1.3 into …/tool/v0.1.3`. If the source
   cannot be had, an in-range cached entry is used if there is one (`-- epm: <reason>;
   using epm 0.1.2 from the cache`), else configure stops.

Step 4 runs under `file(LOCK <cache>/tool/<key>.lock)` (up to an hour); a configure
that waited on the lock finds the marker the first one wrote. An entry without its
marker (with a readable `version`), `bin/epm` and `epm.cmake` is deleted and built anew,
its marker removed first: step 3 reads markers without the lock, and a half-built entry
has `bin/epm` and `epm.cmake` as soon as Debug is installed. The `EPM_SOURCE_DIR` step
holds its entry's lock the same way, at every configure.

### Building an entry

The source is configured, built and `cmake --install`ed with
`--prefix <entry>`, Debug and then Release, so the Release `bin/epm` is the one that
stays (*Install*, below):

- epm is a host program, so its generator is not the consumer's: Ninja when a ninja is
  found (the consumer's `CMAKE_MAKE_PROGRAM` under a Ninja generator, else `ninja` or
  `ninja-build` on PATH, passed as `CMAKE_MAKE_PROGRAM`), else `Unix Makefiles`,
  whatever the consumer uses (Xcode, which cannot build epm's per-config sources,
  Visual Studio, Ninja Multi-Config). On Windows without a ninja, and for a Visual
  Studio consumer there outside a developer prompt (no `VCINSTALLDIR`, so Ninja would
  find no compiler), the consumer's generator is kept, with its toolset and make
  program; otherwise neither of those (`xcodebuild`, MSBuild) reaches the build.
- A single-config generator gets two build directories, `<entry>/build-<generator>-debug`
  and `<entry>/build-<generator>` (`build-ninja`, `build-unix-makefiles`), each with its
  `CMAKE_BUILD_TYPE`; a multi-config generator (only a kept Visual Studio one) gets
  one, `<entry>/build-<generator>`, built and installed with `--config Debug` and
  `--config Release`. Since the generator does not follow the consumer's, consumers
  with different generators share a `local-<hash>` entry's build directories.
- Every configure gets `-DEPM_ENABLE_TESTS=OFF` and, unless the environment sets
  `CPM_SOURCE_CACHE`, `-DCPM_SOURCE_CACHE=<entry>/cpm`, so both builds share one
  download of epm's dependencies.
- It is a host build. Each step runs through `cmake -E env --unset=…`
  (`CMAKE_TOOLCHAIN_FILE`, `CMAKE_GENERATOR`, `CMAKE_GENERATOR_PLATFORM`,
  `CMAKE_GENERATOR_TOOLSET`, `CMAKE_BUILD_TYPE`, `CMAKE_CONFIGURATION_TYPES`,
  `CMAKE_OSX_ARCHITECTURES`, `CMAKE_INSTALL_PREFIX`, `DESTDIR`, and the `SDKROOT`,
  `MACOSX_DEPLOYMENT_TARGET` and `IPHONEOS_DEPLOYMENT_TARGET` an Xcode build exports to
  a CMake re-run it starts), so nothing of the
  consumer's toolchain file, build type or target architectures reaches it, as variable
  or environment; a consumer targeting iOS or Android still gets a macOS or Linux epm.
  A consumer that is not native (below) also has `CC`, `CXX`, `CFLAGS`, `CXXFLAGS` and
  `LDFLAGS` unset, so a cross SDK's environment (Yocto-style) does not leak in.
  It gets the generator above, the consumer's
  `CMAKE_<C|CXX>_COMPILER_LAUNCHER` (ccache; a single program, not a list), and its C and
  C++ compilers and `CMAKE_LINKER_TYPE` when they are known and native (no toolchain
  file, not cross-compiling, `CMAKE_SYSTEM_NAME` the host's): eacp's Release IPO needs
  a linker that reads the compiler's LTO objects, such as lld for clang on Linux. As a
  top-level include get_epm runs before languages
  are enabled, so usually none are known, and the build finds the same default (`CC`,
  `CXX`, the system's) the consumer will. That way epm's recipe libraries come from the
  compiler that builds the consumer's `epm_add_recipe` target (`Docs/Graph.md` §2).
- A build directory is configured once (`epm-configured` is written in it after the
  configure succeeds); later builds are incremental, and the compilers of that first
  configure stay: delete the entry to change them.
- A cold build prints `-- epm: building epm v0.1.3 into <entry> (Debug and Release;
  first time only)`, then one status line per phase (`-- epm: epm v0.1.3: configuring
  Debug`, `building Debug`, `installing Debug`, the same for Release; one `configuring`
  with a multi-config generator). Incremental steps are verbose notes only;
  `EPM_VERBOSE` streams the build. Every command and its output is appended to
  `<entry>/epm-bootstrap.log`, which is kept. A failed step is fatal, with the end of its
  output and the log path.
- Once a tag entry is built, its build directories and `cpm/` are deleted, unless the
  installed epm's recipe SDK names a path inside them (an epm that fell back to its
  build tree, `Docs/Graph.md` §2); `src/` and the log stay. A `local-<hash>` entry keeps
  its build directories for the next incremental build.

### The entry

Every tool entry, whether built from a tag, built from a checkout, or an epm installed
on PATH, is the layout `cmake --install` produces (*Install*, below). get_epm finally
sets `EPM_EXECUTABLE` (cache `INTERNAL`) and
includes `<entry>/share/epm/cmake/epm.cmake`. The pick is also remembered as
`EPM_SELECTED_EXECUTABLE`, so the next configure picks afresh (a newer release, a changed
range, a new PATH install) instead of mistaking it for a user-given `EPM_EXECUTABLE`.
The same goes for a path the pre-get_epm `epm.cmake` bootstrapped
(`EPM_BOOTSTRAPPED_EXECUTABLE`, removed from the cache on the first get_epm configure).

The marker `<cache>/tool/<key>.json` holds `schema` (1), `ref` (the tag or ref, or the
`local-<hash>` key), `origin` (always `source`), `url` (`<repository>@<ref>`, the
tarball, or the checkout's path), `version` and `builtAt`.

Source overrides (each skips steps 2–3 and step 4's choice of tag, and is built as
step 4 builds): `EPM_SOURCE_REF` (tag, branch or commit; entry `tool/<ref>`) and
`EPM_SOURCE_URL` (a source tarball, downloaded instead of fetched with git; entry
`<ref or "url">-<sha256(url)[:12]>`); a local checkout is `EPM_SOURCE_DIR` (step 1).
Entries for branches (`main`) are not refreshed. Refs are sanitised to `[A-Za-z0-9._-]`
for the directory name.

A tag entry is keyed by its tag only, not by compiler: the first configure that builds
`tool/v0.1.3` fixes the compiler of its recipe libraries for every project on the
machine. A project built with another compiler family then stops at `epm_add_recipe`'s
compiler check (`Docs/Graph.md` §2) until that entry is deleted. A known limitation.

The cache root is the one the binary uses: the `EPM_CACHE` CMake variable (passed as
`--cache`), else the `EPM_CACHE` environment variable, else eacp's
`FilePath::cacheDirectory()` + `/epm` mirrored in CMake: `$HOME/Library/Caches` on macOS,
`$XDG_CACHE_HOME` or `$HOME/.cache` on Linux, `%LOCALAPPDATA%` on Windows.

### Install

`cmake --install <build> --config <Config>` of epm installs exactly:

```
bin/epm                     the Release binary: another configuration installs it
                            only into a prefix whose bin/epm is not a Release one
lib/<Config>/               libepm-recipe.a, libeacp-network.a, libeacp-core.a,
                            libfmt.a, libminiz.a, libMiro.a, libResEmbed.a: the
                            recipe libraries, in link order
include/                    epm/ (the recipe headers: the recipe API, Spec, Cache,
                            Build, Util), eacp/Core, eacp/Network (PackageLock.h
                            includes its IPC/Lock.h), Miro (and MiroResources.h),
                            ResEmbed, ea_data_structures: the headers of their
                            public interface
share/epm/cmake/            epm.cmake and epm-provider.cmake
share/epm/epm-config        one line: the configuration of the installed bin/epm, so
                            Release always wins bin/epm and a reinstall of the same
                            configuration refreshes it
```

Installs of several configurations share one prefix. The archives have the same file
names in every configuration (`libfmt.a`, never `libfmtd.a`), and are built without LTO
(plain object code, not the bitcode of eacp's Release LTO, which only an LTO link by the
very same compiler reads). On Linux, `eacp-network` links libcurl: building epm needs its
development package (`find_package(CURL REQUIRED)`), and recipes link it as one of the
SDK's link options, by name rather than by the build machine's path to it: `-lcurl` (or
`-l:<file>` for a versioned soname such as `libcurl.so.4`), after `-L<dir>` only when the
compiler does not search that directory already (`epm_shared_library_options` in
`Lib/epm/CMakeLists.txt`, for every imported shared library); frameworks and static
archives keep their paths. The binary finds its install at run time: `<prefix>` is two
levels above the real executable, and every `lib/*/` holding all the archives is a
configuration (`Docs/Graph.md` §2). Code linking `epm-recipe` that is not the epm binary
(a recipe executable running the configure, which compiles the other recipes) finds it
the same way from `env.json`'s `epmExecutable` (`recipeSdkFor`); without an install
prefix there, the build tree's SDK baked into the library is used. A top-level
single-config build of epm defaults
`CMAKE_BUILD_TYPE` to Release, so it always has a configuration to install.
`epm --version` prints `project(epm VERSION …)` through the generated `Version.h`.

## 9. Configs and ABI matching

**`EPM_CONFIGS`** (cache variable, default `auto`) says which configurations of each
package to build and cache. `epm.cmake` resolves it, since it knows the generator, and
sends the result as the request's `configs`:

- `auto`: a single-config generator gets the consumer's `CMAKE_BUILD_TYPE` (empty,
  `RelWithDebInfo`, `MinSizeRel` or any other type mean Release); a multi-config
  generator (Xcode, Visual Studio, Ninja Multi-Config) gets `Debug;Release`.
- An explicit list, e.g. `Release` or `Debug;Release`, of Debug, Release, RelWithDebInfo
  and MinSizeRel (any case; the last two are built as Release). Anything else stops the
  configure. `epm_resolve_configs` implements it (`Tests/EpmConfigsAuto.cmake`).

Configs are **additive within an entry**: a Ninja Release configure builds only Release;
a later Ninja Debug configure finds that entry, builds only Debug and adds it (its own
`lib/Debug`, `export-Debug.cmake`, `gen/Debug`, `Debug.json`), and an Xcode configure
after that needs nothing. A config already in an entry is never rebuilt.

**`EPM_ABI_MATCH`** (cache variable, default `compatible`, sent as `abiMatch`) says which
existing entries a request may use, comparing the entry's recorded fingerprint (§3) with
the current one:

| policy       | must be equal                                                        |
|--------------|----------------------------------------------------------------------|
| `strict`     | every recorded field (the 0.1.2 behaviour)                           |
| `compatible` | platform, compilerId, compilerMajor, osxDeploymentTarget, cxxStandard, options, sourceId, toolchainFileHash |
| `any`        | platform, options, sourceId                                          |

`compatible` ignores the compiler path, its minor and patch version and
`systemProcessor` (the platform already encodes the architecture), so Xcode's clang and
the command-line tools' clang share entries. When several entries qualify the one picked
(`pickEntry`) is: one holding any config at all before an empty one (what a failed build
leaves, §5), then an exact (strict) match, then one holding every needed config, then the
one holding most of them, then the newest. With none, a new entry is created for the current
fingerprint. Missing configs are built into whichever entry was picked, with the current
toolchain (its `<Config>.json` records which). Using an entry that is not an exact match
adds a note to the response and the status line:
`-- epm: eacp (prebuilt, macos-universal, 1a2b3c4d5e6f, built with AppleClang
17.0.0.17000013, current 17.0.0.17000319)`.

**Platform names** come from `env.json` (`platformName`, `Spec/Platform`):
`macos-universal` (`CMAKE_OSX_ARCHITECTURES` holds both arm64 and x86_64), `macos-arm64`,
`macos-x64`; `ios-arm64`, and `ios-simulator-arm64` / `ios-simulator-x64` /
`ios-simulator-universal` when `CMAKE_OSX_SYSROOT` contains "simulator" (any case), the
architecture from `osxArchitectures` as for macOS, which `epm.cmake` fills when the consumer
leaves it empty (§2: an Xcode simulator consumer is `ios-simulator-universal`, a Ninja one
on Apple Silicon `ios-simulator-arm64`); `tvos-`, `watchos-` and `visionos-` alike;
`windows-x64`, `windows-arm64`,
`linux-x64`, `linux-arm64` (x86_64/AMD64 → x64, aarch64 → arm64); `android-<abi>`;
anything else `<system name>-<processor>` in lower case. release.yml names its archives
after the five desktop names.
