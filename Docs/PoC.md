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
  "generatorPlatform": "", "generatorToolset": "", "generatorInstance": "",
  "makeProgram": "/opt/homebrew/bin/ninja",
  "isMultiConfig": false,
  "buildType": "Debug", "configurationTypes": [],
  "cCompiler": "/usr/bin/cc", "cCompilerId": "AppleClang",
  "cCompilerVersion": "17.0.0.17000013", "cxxCompiler": "/usr/bin/c++",
  "cxxCompilerId": "AppleClang", "cxxCompilerVersion": "17.0.0.17000013",
  "cxxCompilerFrontend": "GNU",
  "systemName": "Darwin", "systemProcessor": "arm64", "crossCompiling": false,
  "osxArchitectures": "", "osxDeploymentTarget": "11.0",
  "osxSysroot": "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
  "osxSysrootPath": "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
  "sdkVersion": "15.2",
  "androidAbi": "", "androidApi": "", "androidNdk": "", "androidStl": "",
  "cxxStandard": "20",
  "toolchainFile": "", "msvcRuntime": "",
  "profile": "",
  "epmExecutable": "/path/to/epm" }
```

`osxSysroot` is `CMAKE_OSX_SYSROOT` (an SDK name such as `iphonesimulator` under Xcode,
the resolved path under other generators) and `androidAbi` is `CMAKE_ANDROID_ARCH_ABI`
(else the NDK toolchain's `ANDROID_ABI`); both feed the platform name (§9), and a
cross-compiling child build gets them (§5). The other Android settings, empty off
Android (`epm_android_setting`), are the API level `androidApi` (the NDK legacy
toolchain's `ANDROID_PLATFORM_LEVEL`, else `CMAKE_SYSTEM_VERSION`, which that toolchain
file sets to 1), `androidNdk` (`CMAKE_ANDROID_NDK`, else `ANDROID_NDK`) and `androidStl`
(`CMAKE_ANDROID_STL_TYPE`, else `ANDROID_STL`); a child build gets them too (§5), and the
API level and STL are in the fingerprint (§3). `androidNdk` is a path, so it is not: the
NDK counts by its revision, `sdkVersion` (below). `crossCompiling` is `CMAKE_CROSSCOMPILING`:
the consumer named its target system itself. For an Apple device or simulator SDK (iOS,
tvOS, watchOS, visionOS), whose `CMAKE_SYSTEM_PROCESSOR` CMake leaves empty, an empty
`CMAKE_OSX_ARCHITECTURES` is written as what the SDK builds for by default: Xcode's
`ARCHS_STANDARD` (`CMAKE_XCODE_ARCHS`, `arm64;x86_64` for a simulator), else the
compiler's default target (`CMAKE_CXX_COMPILER_ARCHITECTURE_ID`), so the platform name and
the fingerprint carry the architecture and child builds build exactly it; and an empty
`CMAKE_OSX_DEPLOYMENT_TARGET` as `CMAKE_XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET` (`TVOS_`,
`WATCHOS_`, `XROS_` alike) when the project sets that instead (`epm_osx_architectures`,
`epm_osx_deployment_target`). Failing both, `osxDeploymentTarget` is the environment's
`MACOSX_DEPLOYMENT_TARGET` (`IPHONEOS_`, `TVOS_`, `WATCHOS_`, `XROS_` for the device
systems), which clang reads when it is given no target: CMake reads it into
`CMAKE_OSX_DEPLOYMENT_TARGET` only on a build tree's first configure, so a later change,
or a variable left empty, would otherwise leave the key without the target the compiler
uses. Child builds get the value as an explicit `CMAKE_OSX_DEPLOYMENT_TARGET` (§5), so
their own environment cannot change it.
`sdkVersion` (`epm_sdk_version`) is the version of the SDK the consumer compiles
against, for the fingerprint (§3, §9). For an Apple system it is the `Version` of the
`SDKSettings.json` in `osxSysrootPath` (`epm_osx_sysroot_path`): the SDK directory CMake's
Darwin platform module resolved (`_CMAKE_OSX_SYSROOT_PATH`, set also when the compiler's
default SDK is used, as named by `SDKROOT` or `xcode-select`), else `CMAKE_OSX_SYSROOT`
when that is a path, else what `xcrun [--sdk <name>] --show-sdk-path` names; empty off
Apple systems and when the directory does not exist. Every Apple child build gets
`osxSysrootPath` as its `CMAKE_OSX_SYSROOT` (§5), so it compiles against the SDK whose
version the key records rather than whatever its own environment would pick; the path
itself is in no fingerprint. For Windows `sdkVersion` is
`CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION` (Visual Studio generators), else a developer
prompt's `WindowsSDKVersion`. For Android it is the NDK's revision, the `Pkg.Revision`
of `<androidNdk>/source.properties` (`27.0.12077973`), which stands for the NDK's sysroot
and STL as an Apple SDK's version does for that SDK, while `androidNdk`, its directory,
stays out of the key. Empty elsewhere and when none is found.
Empty string / empty array when a variable is unset. Written at the first `epm_add_package`
call (not at include time), once per configure. `cCompiler` (`epm_c_compiler`) is the C
compiler of every child build: the consumer's `CMAKE_C_COMPILER`; when the consumer does
not enable C, on the MSVC ABI the C++ compiler (cl and clang-cl compile both, and CMake
refuses a build mixing clang-cl with the cl it would pick for C), elsewhere the C driver
beside the C++ one (`epm_c_driver_beside`: `clang` for `clang++`, `gcc` for `g++`, `cc`
for `c++`, `icx` for `icpx`, `emcc` for `em++`, any prefix and suffix kept, the C++
compiler's symlinks followed one link at a time so an alternatives `c++` that is
`g++-13` gives `gcc-13`). A child build given no C compiler would take `CC` or CMake's
default, which the key does not record. It is empty only when no such driver exists;
the binary never passes an empty `-DCMAKE_C_COMPILER`. `cCompilerId` and
`cCompilerVersion` are `CMAKE_C_COMPILER_ID`/`_VERSION` when the consumer enables C,
else empty; the fingerprint records them only when they differ from the C++ compiler's
(§3).
`makeProgram` (`CMAKE_MAKE_PROGRAM`) is passed to a child build that reuses the consumer's
generator, so a build tool that is not on PATH (CLion's bundled ninja) is still found.
`generatorPlatform` (`CMAKE_GENERATOR_PLATFORM`, else the `CMAKE_VS_PLATFORM_NAME` a
Visual Studio generator picked), `generatorToolset` (`CMAKE_GENERATOR_TOOLSET`) and
`generatorInstance` (`CMAKE_GENERATOR_INSTANCE`) are passed with a Visual Studio
consumer's generator when a child build reuses it (§5).
`epmExecutable` is `EPM_EXECUTABLE`, the epm binary running the configure: the recipe SDK
is found from its install prefix (`RecipeSdk`, §8), and a child build with dependencies
gets it as its `EPM_EXECUTABLE`. Like `profile`, it is in no fingerprint.
`cxxCompilerFrontend` is `CMAKE_CXX_COMPILER_FRONTEND_VARIANT`: `MSVC` for cl and for
clang-cl, which takes cl's command line and targets the MSVC ABI (`msvcAbi`). There,
and only there, `msvcRuntime` is the runtime library the consumer's targets get
(`CMAKE_MSVC_RUNTIME_LIBRARY`, or CMake's own default, the DLL one, when the consumer
left it empty): `epm.cmake` defaults the variable to the static runtime,
`MultiThreaded$<$<CONFIG:Debug>:Debug>`, epm's own (`Docs/Graph.md` §2), in the scope
that includes it, before any target. Every child build is given the same value (§5), and
it is part of the fingerprint (§9): objects and archives compiled against different
runtimes do not link (LNK2038). A consumer whose policy CMP0091 is not NEW would ignore
the variable while its dependencies honour it, so `epm_write_env` stops the configure
there.

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
an empty `configs`, a config other than Debug, Release, RelWithDebInfo or MinSizeRel
(MinSizeRel is built as Release) and an unknown `abiMatch`.

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
set(epm_eacp_SOURCE_DIR "")    # source: "/Users/me/.cache/epm/src/eacp/3f1c0a9b2d4e"
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
cases it sets `<name>_SOURCE_DIR` (empty when prebuilt: the source is fetched only to
build, §4), `<name>_ADDED` and `<name>_MODE` in the caller's scope,
and prints one line: `-- epm: eacp (prebuilt, macos-arm64, 9a8b7c6d5e4f)`, with the note
appended when there is one (`-- epm: eacp (prebuilt, macos-arm64, 9a8b7c6d5e4f, built
with AppleClang 17.0.0.17000013, current 17.0.0.17000319)`), or
`-- epm: eacp (source, 3f1c0a9b2d4e)`.

## 3. Cache layout (PoC subset of PLAN.md §5.3)

```
$EPM_CACHE  (default: <platform cache dir>/epm, e.g. ~/Library/Caches/epm or ~/.cache/epm)
  src/<name>/<sourceId12>/          clean snapshot, no .git
  src/<name>/<sourceId12>.json      completeness marker: {url, commit, fetchedAt}
  src/<name>/.recipes/<sourceId12>/ the package's epm.cpp alone (empty without one),
                                    what the graph walk reads and compiles, the snapshot
                                    there or not (§4): a recipe is one file; staged in
                                    .recipes/ and appears whole, by one rename
  bin/<name>/<platform>/<fp12>/     one entry per ABI fingerprint (§9), schema 3; it appears
                                    whole, by one rename, holding manifest.json, the facade
                                    and an empty configs/
      manifest.json                 {schema: 3, fingerprint, sourceId, createdAt}
      epm-targets.cmake             the facade (§5): the same file in every entry, never
                                    rewritten; it finds the published configs when loaded
      configs/<Config>/             one config's unit: built in staging, published by one
                                    rename, never modified afterwards
          epm-config.json           the config's marker, inside the unit, so the config is
                                    published exactly when it is: {config, builtAt,
                                    compilerId, compilerVersion, compilerPath, lto,
                                    filesSha256}; small, the only file a pick reads
          epm-files.json            published with it: {files: [{path, size, sha256}],
                                    shared: [...]}, every file the config depends on,
                                    read only to verify or copy it; the marker records
                                    its sha256
          lib/<Config>/*.a|*.lib
          export-<Config>.cmake (+ .names, .aliases)   CMake export() output, namespaced
                                    epm_<config>::, every path in it ${CMAKE_CURRENT_LIST_DIR}/…
                                    (the unit) or, for gen/shared, ${_epm_entry_dir}/… (§5)
          gen/<Config>/…            relocated build-tree paths (include dirs, objects)
          include/src/<relpath>     what the export takes from the source snapshot (header
                                    trees, by path relative to it)
          include/cpm/<relpath>     the same from <cache>/cpm (nested CPM deps)
      gen/shared/<sha12>/<relpath>  relocated INTERFACE_SOURCES files, content-addressed,
                                    shared by the configs and add-only
                                    The entry names nothing outside itself, so it can be
                                    moved or copied, alone or with the whole cache.
  bin/<name>/<platform>/.staging/<tmp>.<pid>-<host12>.<unix seconds>/   a build's private
                                    directory, laid out as an entry (configs/<Config>/,
                                    gen/shared/), or an entry being assembled; never read;
                                    deleted once published, and as stale by the next build
                                    (removeStaleStaging): at once when its process, on this
                                    host, is gone; one of another host when its
                                    .epm-heartbeat (rewritten before each config a build
                                    builds), else its name's time, is over a week old
  build/<name>/<platform>/<fp12>/<staging name>/<Config>/   child build trees, one set per
                                    staging directory, with its own .epm-heartbeat; deleted
                                    after the harvest (a failed one is kept, then deleted
                                    as stale by the same rule)
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
step, in its own process or another, holds none meanwhile. For binary entries the lock
only keeps two local processes from building the same thing twice; their correctness
does not depend on it, since entries and configs are published atomically (§5), so two
processes, or two hosts sharing the cache on one filesystem, that build the same config
both end with a valid entry. Readers (picking an entry, the facade) take no lock. A recipe's compile holds `recipe-<key12>` the same way. `IPC::Lock` keeps its lock
files in its own per-user directory (`<appData>/eacp/Locks`) and takes no path, so the lock
is named `epm-<sha256(cache root)[:12]>-<name>` to keep separate caches independent; there
is no `locks/` directory. The cache root is resolved (`weakly_canonical`) before use so
every path epm writes and matches has one spelling.

`sourceId = sha256("git\n" + repositoryKey(url) + "\n" + commit)`, first 12 hex chars
as directory name. `repositoryKey` (`Docs/Graph.md` §5 rule 5) is one spelling per
repository: the scheme, a `user@`, trailing `/` and `.git` dropped, scp-like
`git@host:owner/repo` as `host/owner/repo`, lower case, so `git@github.com:o/r.git` on a
laptop and `https://github.com/o/r` on CI give the same `github.com/o/r` and share the
snapshot and every entry built from it; another host, owner or repository (or a non-default
ssh port, which is kept) stays distinct. A local path or `file://` URL is made absolute
and resolved, keeping its case: a local repository is machine-specific anyway. Fetching
always uses the URL as written, and the snapshot's marker records the first one used.

`fp12` is the first 12 hex chars of `sha256(canonical fingerprint JSON)`. The fingerprint
(`AbiFingerprint`, recorded in `manifest.json`) holds `schema` (4), `platform`,
`compilerId`, `compilerVersion` (full), `compilerMajor`, `cCompilerId` and
`cCompilerVersion` (§2: set only when the consumer enables C with a compiler whose id or
version is not the C++ compiler's; empty for the same toolchain and for a C driver found
beside the C++ one, so a C++-only consumer and a C and C++ one share keys), `systemName`,
`systemProcessor`, `osxArchitectures`, `osxDeploymentTarget`, `sdkVersion` (§2: the
Apple or Windows SDK compiled against, or the Android NDK's revision; empty elsewhere),
`cxxStandard`, `androidApi` and `androidStl` (the API level and STL, §2; empty off
Android, whose ABI the platform name carries), `msvcRuntime`
(the runtime library on the MSVC ABI, §2; empty elsewhere), `toolchainFileHash` (sha256
of the toolchain file's contents, when there is one), `envFlags`, `options` (sorted),
`sourceId`, `deps`, `compatibleDeps` and `anyDeps` (`Docs/Graph.md` §10). It holds no
machine path, so another machine (a CI runner, a colleague, a shared cache) with the same
toolchain, SDK and source computes the same key: the compiler counts by id and version,
the toolchain file by its contents, the Android NDK by its revision, never by where they
are (a config's marker still
records the compiler path that built it, for diagnostics). `envFlags` holds the environment variables a
child build inherits from epm and that change what it compiles, by name, those set and
not empty: `CFLAGS`, `CXXFLAGS`, `OBJCFLAGS`, `OBJCXXFLAGS`, `ASMFLAGS`, `CUDAFLAGS`,
`HIPFLAGS`, `FFLAGS` and `LDFLAGS`, which CMake reads into a build tree's initial
`CMAKE_<LANG>_FLAGS` and linker flags, and `CL` and `_CL_`, which cl and clang-cl read
on every call. They are read from the process that runs the build step (the child build
is its child), not from `env.json`. Schema 4 is where paths left the key and
`sdkVersion`, `envFlags`, `androidApi`, `androidStl` and the normalized `sourceId` came
in; every entry of an earlier schema is ignored and rebuilt. The canonical form is Miro's
compact output of the
struct (`Miro::toJSONString`): Miro objects are `std::map`s, so every key, options
included, comes out sorted, with no whitespace. The configs are *not* in it: one entry
holds every config built for that fingerprint, added one at a time. Which entry a request
may use is decided by `EPM_ABI_MATCH` (§9), so an Xcode consumer can use the entry a Ninja
consumer built with `/usr/bin/c++`. Full toolchain probing (compiler content hash, macros)
comes later.

Entries of the 0.1.2 layout (`bin/<name>/<key12>/` with `<key12>.json` beside it) are
ignored, never read or deleted; `epm cache ls` reports them. Delete `bin/` once after
upgrading (it only holds rebuildable binaries). Entries of the schema 2 layout (configs
built in place at the entry's top level, `<Config>.json` markers) are ignored too, and
one at the path a new entry needs is moved aside and deleted when it is created.

**Completeness and immutability.** A config is in an entry exactly when
`configs/<Config>/epm-config.json` is there and names it: the unit is renamed into place
with its marker inside, so no reader ever sees half a config, and epm never re-hashes on
a configure. Nothing of a published config changes afterwards: adding Debug to a Release
entry publishes `configs/Debug/` beside it and leaves every Release file and the facade
byte for byte as they were. The only shared files, `gen/shared/<sha12>/…`, are
content-addressed and add-only: each is published by its own rename, and one already
there (the same contents, by its path) is kept. A crash leaves only `.staging`
directories, which readers never look at.

`epm cache ls [--cache <dir>]` prints one line per binary entry
(`<name>  <platform>  <configs>  <compiler> <major>  <size>  <fp12>`), then the sources and
the tools; `epm cache path` prints the cache root. `epm cache verify [--cache <dir>]`
re-hashes every published config against its `epm-files.json` (which must be the one
the marker recorded: each listed file of the unit and each `gen/shared` file its export
names, missing, another size or another sha256; and any file of the unit it does not
list) and also reports an entry directory without a readable manifest, a facade that is
not epm's, a unit without a valid marker, and a unit whose name is not a config's
canonical spelling (`configs/release`, which no pick reads); it prints one line per
problem (`<name>/<platform>/<fp12> <Config>: <file>:
<problem>`) and a summary (also counting older-layout entries, which it skips, and
staging directories), and exits 1 when there is any problem.

### Cache stores

A **cache store** is a directory laid out as a cache: `<store>/bin/<name>/<platform>/<fp12>/`
exactly as above (a store may hold only `bin/`). It is a network share, a synced
folder, an unpacked CI artifact: anything another machine filled with `epm cache push`
(or a copy of another cache). epm reads stores and never writes to them during a
configure; nothing in one is locked, created or deleted.

- **Configuration.** `EPM_CACHE_STORES`: a CMake list (`epm.cmake` passes each entry,
  made absolute against the source directory, as `--cache-store <dir>`), else the
  environment variable of that name, its paths separated by the platform's path-list
  separator (`:`, `;` on Windows; `CacheRoot::storesFromEnvironment`). `epm configure`,
  `epm resolve` and a recipe executable's `configure` take `--cache-store` (repeated, in
  order); without one they read the environment. `epm configure` resolves the list once
  and passes it explicitly to the recipe executable, and the configure records it in
  every build step's input (`BuildInput::cacheStores`, `<build>/epm/steps/<name>.in.json`),
  so a step in another recipe executable, or one run again by hand from
  `steps/<name>.command`, uses the same stores whatever its environment.
  `epm_add_recipe`'s `<target>-configure` passes them too. Stores are in no fingerprint.
- **A store that is missing or unreadable** is a note (`-- epm: note: the cache store
  <dir> does not exist: not used`, once per configure), never an error.
- **Precedence.** For each package not in `source` mode, `Package::cached` picks from
  the local cache first (§9). Only when that pick lacks a needed config (one this
  compiler can link) are the stores searched, in order (`pullFromStores`,
  `Cache/CacheStore`), and only until the local pick holds every needed config. Then,
  if configs are still missing, they are built as before (or the mode refuses: a
  `binary-only` request fails naming the cache and its stores). A store hit counts as
  cached in every mode: `binary-only` and `prebuilt` use it, and `auto` with
  `EPM_BUILD_MISSING_BINARIES=OFF` uses it instead of falling back to source.
- **Picking across the cache and a store.** For one store, the store's entries of the
  package are added to the local ones config by config, an entry of the same `fp12`
  gaining the store's configs, and `pickEntry` picks among them under the request's
  `EPM_ABI_MATCH`, with its usual order and LTO rule. When that pick holds more of the
  needed configs than the local pick alone, the configs it holds that the local cache
  lacks are copied into the local entry of that `fp12` (created from the store entry's
  fingerprint when missing). So a local entry holding Release and a store entry of the
  same fingerprint holding Debug end as one local entry holding both; a store entry of
  another fingerprint that adds nothing the local pick lacks is never copied. A store
  entry whose directory is not named after its fingerprint is ignored.
- **Copying** (`copyConfig`) uses the protocol of a build (§5): the unit's files listed
  in the store's `epm-files.json` (checked against the marker's `filesSha256`), and the
  `gen/shared` files it names that the local entry lacks, are copied into a fresh staging
  directory of the local entry, each checked against its recorded size and sha256 as it
  is read, and the unit is published by one no-replace rename with the same marker. A
  store comes from other machines, so its markers are checked before anything is
  trusted: a path that climbs out of its directory, a shared path outside
  `gen/shared/` (one under `configs/<Config>/` would block that config's publish for
  good), a unit that lists no `export-<Config>.cmake` or no `.names` (the facade reads
  both), an `lto` the copied bytes do not bear out (`ltoKindOf` of each file as read, in
  either direction), an LTO config recorded for a compiler other than the entry's (epm
  publishes LTO objects only into their compiler's exact entry), and a file that is
  missing or differs each fail that unit; a unit whose directory and marker spell a
  config other than canonically is never read at all. Nothing is published, a note names the
  store, the package and the problem (`-- epm: note: the cache store <dir> holds Release
  of eacp (1a2b3c4d5e6f), but it cannot be used: lib/Release/libeacp.a: sha256 differs
  from the recorded one`), and the pick is made again without it, so the next store, or
  a local build, takes over. A copied unit is announced (`-- epm: eacp: copied Release
  from <store> into the cache`). The local entry is then used as if it had been built
  there: every path a consumer or a child build sees is in the local cache.
- **Picking.** A complete compatible entry (every needed config, linkable by this
  compiler) ranks above an exact one that lacks a config (§9), so a store entry of a
  compatible compiler holding Debug and Release is pulled in preference to building
  the Release a local exact entry lacks.
- **Source fetch.** A package found in the cache or a store is used without building
  it; its source snapshot is fetched afterwards only for the consumer's `SOURCE_DIR`,
  which `EPM_FETCH_SOURCES=OFF` turns off (§4, `Docs/Graph.md` §7).

`epm cache push <store> [--cache <dir>] [<name>...]` copies every published config of
the local cache (only the named packages' when names are given) into the store with
the same `copyConfig`: entries are created in the store as a build creates them
(`createEntry`, one rename), each config is staged in the store's `.staging` and
published there by a no-replace rename, so several CI jobs pushing into one share at
once each end with a valid store, the loser of a race dropping its copy. A config the
store holds already is skipped, and only units with a valid marker are pushed, each file
checked against it while copying (a corrupt local unit is reported, not pushed); staging
debris never is. It prints the problems, then `pushed 3 configs (24.1 MB) into 2 entries
to <store>; 1 already there`, and exits 1 when there was a problem. `epm cache pull
<store> [--cache <dir>] [<name>...]` is the reverse, filling the local cache ahead of
time (a configure pulls on demand anyway).

Caching the whole `EPM_CACHE` directory with a CI system's cache step is the simpler
alternative within one job's runs: it keeps sources, recipes and the bootstrapped epm
too. Stores are for sharing between jobs, machines and people. HTTP, S3 or release-asset
backends, signing and garbage collection of stores are future work.

## 4. Source fetch

1. Resolve `gitRef` to a commit: 40 hex chars → as is; otherwise `git ls-remote <url>` and
   pick, in order, `refs/tags/<ref>^{}`, `refs/tags/<ref>`, `refs/heads/<ref>`, then an
   exact ref name (`HEAD`, `refs/...`).
2. Under the per-name lock, if the marker exists → done. Else into `tmp/<random>/`:
   `git init`, `git remote add origin <url>`, `git fetch --depth 1 origin <commit>`,
   `git checkout --detach FETCH_HEAD`, `git submodule update --init --recursive --depth 1`
   (if any), delete `.git` (and submodules' `.git` files), rename into place, write the
   marker. A server that refuses the shallow fetch by commit gets `git fetch origin <ref>`
   (every branch and tag for a bare hash) and `git checkout --detach <commit>`: `<ref>` is
   the tag or branch the commit was resolved from, carried along with it wherever the
   commit is fetched later (`fetchCommit`; the step's request, `dependencySources`, the
   consumer's `SOURCE_DIR` fetch), never the commit itself.
3. Shell out to the `git` CLI through `eacp::Processes` with a non-interactive environment
   (`GIT_TERMINAL_PROMPT=0`). Lift the wrapper from
   `/Users/eyalamir/Code/AgentWorkspace/Lib/WorkspaceCore/Git.{h,cpp}`.

**Resolving is not fetching.** The fingerprint needs only the `sourceId`, which is the
repository key and the commit of step 1 (`resolveSource`: an `ls-remote`, nothing for a
full hash), so nothing fetches the snapshot until something reads it:

- The graph walk (`Docs/Graph.md` §5) needs each package's `epm.cpp` only
  (`fetchManifest`), always from `src/<name>/.recipes/<sourceId12>/`: copied from the
  snapshot when that is complete in the cache, else filled by `git init`, `git fetch
  --depth 1 --filter=blob:none origin <commit>` (the commit's trees, no file contents),
  `git ls-tree` for `epm.cpp` and, when it is there, `git checkout FETCH_HEAD -- epm.cpp`,
  whose contents git fetches on demand; staged inside `.recipes/` (one filesystem, so
  the rename is one) and renamed into place. A server that ignores the filter sends the
  commit's files (git warns, the result is the same); one that refuses the fetch gets the
  full fetch above instead, and the `epm.cpp` is copied from that snapshot. A recipe is
  one file (its compile is keyed by its contents alone, `Docs/Graph.md` §2), and this is
  enforced: it is compiled from beside nothing else of its repository, so a recipe that
  `#include`s a file of its repository fails the same way on every machine, rather than
  only where the snapshot is not cached. The root recipe, the consumer's own `epm.cpp`,
  is compiled where it is; so is the `epm.cpp` of an `EPM_<Name>_SOURCE` checkout.
- The build step gets the commit and `sourceId` (`BuildInput::source`, with a directory
  only when the snapshot is there already) and fetches by that commit only when it builds
  a config, inside `cached`'s callback, under the package lock it holds then
  (`fetchSource(…, lockHeld)`), or when the package is in source mode. A package found in
  the cache or a cache store (§3) is used with no fetch at all.
- A child build that loads its dependencies' graph (`Docs/Graph.md` §10) and a package the
  consumer builds from source get their prebuilt dependencies' snapshots fetched then, as
  their `<name>_SOURCE_DIR` (`Docs/Graph.md` §7).

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
process may have finished it) or creates it (`createEntry`: `manifest.json`, the facade
and `configs/` assembled in `.staging` and renamed into place; a process that loses that
race takes the winner's), and makes a fresh staging directory
(`bin/<name>/<platform>/.staging/<tmp>.<pid>-<host12>.<unix seconds>`, the `Prefix`'s
`stagedEntry()`, §3).
Then `CMakeBuild::run` configures, builds and harvests each missing config, and only
those, in turn, into its unit there, `<staging>/configs/<Config>/` (`<prefix>` below;
child build dir `build/<name>/<platform>/<fp12>/<staging name>/<Config>`, made fresh, so
two processes never share one):

```
cmake -S <src> -B <builddir> -G <generator>
      [-A <platform> -T <toolset> -DCMAKE_GENERATOR_INSTANCE=…]
                                                     # a Visual Studio consumer's generator (below)
      -DCMAKE_BUILD_TYPE=<Config>
      -DCMAKE_TOOLCHAIN_FILE=…                       # the consumer's, if any
      -DCMAKE_SYSTEM_NAME=… -DCMAKE_SYSTEM_PROCESSOR=…
                                                     # cross-compiling without a toolchain file
      -DCMAKE_OSX_SYSROOT=…                          # Apple systems: the consumer's SDK directory
      -DCMAKE_C_COMPILER=… -DCMAKE_CXX_COMPILER=…
      -DCMAKE_OSX_ARCHITECTURES=… -DCMAKE_OSX_DEPLOYMENT_TARGET=… -DCMAKE_CXX_STANDARD=…
      -DCMAKE_MSVC_RUNTIME_LIBRARY=… -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
                                                     # the MSVC ABI only: the consumer's runtime (§2)
      -DEPM_FILE_PREFIX_MAPS=<cache>=/epm/cache;<snapshot>=/epm/src/<name>;<builddir>=/epm/build/<name>;<cache>/cpm=/epm/cpm
                                                     # off the MSVC ABI only (below)
      -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=<prefix>/lib/<Config>
      -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY_<CONFIG>=<prefix>/lib/<Config>
                                                     # so Visual Studio adds no <Config>/ to it
      -DCPM_SOURCE_CACHE=<cache>/cpm
      -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<builddir>/epm-export.cmake
      -DEPM_EXPORT_FILE=<prefix>/export-<Config>.cmake
      -DEPM_EXPORT_NAMESPACE=epm_<config>::          # epm_debug:: / epm_release::
      -D<OPTION>=<VALUE> …                           # the request's, as CMakeBuild::option left them
      -D<NAME>=<VALUE> …                             # CMakeBuild::define, last
cmake --build <builddir> --config <Config> --parallel
                                                     # a multi-config generator (Visual Studio)
                                                     # would build Debug without --config
```

A package with dependencies also gets the nested flags of `Docs/Graph.md` §10. The options
and definitions are part of the fingerprint (`Package::fingerprint(source, build)`), so
the entry is keyed by what was actually passed.

- Generator: the consumer's generator (and `CMAKE_MAKE_PROGRAM`) if it is single-config;
  otherwise `Ninja` if found on PATH (passed as `CMAKE_MAKE_PROGRAM`), else the ninja a
  `Ninja Multi-Config` consumer runs (its `CMAKE_MAKE_PROGRAM`) with `Ninja`, else a
  Visual Studio consumer's generator with its platform (`-A`), toolset (`-T`) and
  instance (`CMAKE_GENERATOR_INSTANCE`) from `env.json`, else `Unix Makefiles` (or the
  platform default on Windows, the only case left without `-G`); `CMakeBuild::generator`
  overrides it. Naming the generator matters beyond choosing it: without `-G`, CMake takes
  the generator from the environment's `CMAKE_GENERATOR` and with it
  `CMAKE_GENERATOR_PLATFORM`, `_TOOLSET` and `_INSTANCE`, which are in no key. `-D` flags
  whose env value is empty are left out; `-DCMAKE_TOOLCHAIN_FILE` is passed when the
  consumer has one.
- Sysroot: every Apple child build (macOS included, cross-compiling or not, with a
  toolchain file or without) gets `env.json`'s `osxSysrootPath` as `CMAKE_OSX_SYSROOT`, the
  SDK directory the consumer compiles against (§2), so it compiles against the SDK whose
  `sdkVersion` the key records: left to itself, a native macOS child would take its own
  `SDKROOT` or `xcode-select` default. Without a resolved directory, a consumer
  cross-compiling without a toolchain file passes its `CMAKE_OSX_SYSROOT` as it is (an SDK
  name such as `iphonesimulator`).
- Target system: when the consumer cross-compiles without a toolchain file
  (`crossCompilesWithoutToolchain`, `Spec/Platform`: `env.json`'s `crossCompiling` set
  and its `toolchainFile` empty), as with `-DCMAKE_SYSTEM_NAME=iOS
  -DCMAKE_OSX_SYSROOT=iphonesimulator`, the child gets `CMAKE_SYSTEM_NAME` and
  `CMAKE_SYSTEM_PROCESSOR` (each when non-empty; CMake leaves the processor empty for
  iOS), the sysroot above, and for Android `CMAKE_ANDROID_ARCH_ABI`, `CMAKE_ANDROID_NDK`,
  `CMAKE_SYSTEM_VERSION` (the API level) and `CMAKE_ANDROID_STL_TYPE`, so it configures
  for that system before its `project()` instead of for the host; with the architectures
  and deployment target above, an iOS simulator consumer's packages are simulator
  archives (`Tests/GraphIos.cmake`). A toolchain file, which the child gets too, names
  the system itself, and a native consumer gets none of these but the sysroot, except
  that an Android consumer's toolchain file (the NDK's `android.toolchain.cmake`) is also
  given `ANDROID_ABI`, `ANDROID_PLATFORM` (`android-<api>`) and `ANDROID_STL`, which it
  would otherwise default (armeabi-v7a, its minimum API level, c++_static). Both ways
  are covered by `Tests/GraphAndroid.cmake`. Recipe executables are still compiled with
  the recipe SDK's host compiler, never the consumer's.
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
- `EPM_FILE_PREFIX_MAPS` keeps the cache out of what the compiler writes into the
  archives (`__FILE__`, debug info): `epm-export.cmake` adds each pair as a
  `-ffile-prefix-map=<old>=<new>` compile option of the top-level directory, which every
  target inherits, so it adds to the flags the build already has (`CMAKE_<LANG>_FLAGS`, a
  toolchain file's) instead of replacing them, and only for C, C++, Objective-C and
  Objective-C++ compiled by a GCC or Clang that takes the option (`COMPILE_LANG_AND_ID`
  and `VERSION_GREATER_EQUAL` on `$<LANG_COMPILER_VERSION>`, per language: GCC 8,
  Clang 10, AppleClang 12, any IntelLLVM; older ones reject it, which would fail every
  compile, so they get none; `Tests/PrefixMapVersions.cmake`). The maps are the cache
  root (`/epm/cache`: the dependencies' snapshots and entries a nested child build
  reads), the package's snapshot root (`/epm/src/<name>`, the whole snapshot even when
  `-S` is a subdirectory of it), the build tree and the CPM cache, each as written and
  with its symlinks resolved (`filePrefixMaps`). The cache root's comes first: GCC
  applies the last map that matches and Clang the last or, by version, the longest
  (AppleClang 21 the longest, checked), so either way the more specific maps after it
  win. The MSVC ABI gets none:
  cl has no documented equivalent (`/d1trimfile:` is undocumented and covers `__FILE__`
  alone, and the linker's `/pdbsourcepath:` concerns PDBs, not static libraries), and
  clang-cl, which takes it as `/clang:-ffile-prefix-map=…`, is left with cl until it is
  tested. There the archives still name the cache in their debug info, which costs a
  moved entry nothing but the debugger's way to the sources.
- Because archives are written straight into the unit, the export file points at
  staged `.a` files; include directories point into the source snapshot and into
  `<cache>/cpm` for nested CPM dependencies, and generated ones into the build tree. An
  entry must work wherever it is moved or copied (a shared cache, another machine), so
  the harvest makes it carry all of that and name it relative to itself.
- **The harvest**, per config, makes the unit's `export-<Config>.cmake` name only files
  inside the entry: the unit's as `${CMAKE_CURRENT_LIST_DIR}/<path>` (the export file
  lives at the unit's top level, so CMake expands that to the unit wherever it is), and
  `gen/shared` ones as `${_epm_entry_dir}/<path>`, set by a first line the harvest adds,
  `get_filename_component(_epm_entry_dir "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)`,
  which normalises the path so every config spells a shared file the same way:
  1. Every build-tree file listed in an `INTERFACE_SOURCES` property is copied to
     `<staging>/gen/shared/<sha256(contents)[:12]>/<path relative to builddir>` (unless
     already there), and that whole path is replaced. Verified against eacp: ResEmbed's
     registration `.cpp` files (Miro, eacp-webview-bridge).
  2. Every other path under `<builddir>` (or `<builddir>` itself) is copied to the
     unit's `gen/<Config>/<path relative to builddir>`: ResEmbed-generated include dirs,
     and the `IMPORTED_OBJECTS` of OBJECT libraries (MiroFormats, MiroCodegenWriter).
  3. Every path under the source snapshot goes to the unit's `include/src/<relative
     path>`, and every path under `<cache>/cpm` to its `include/cpm/<relative path>`,
     lexically normalised (eacp's `Lib/eacp/Core/../../` is `include/src/Lib`); a
     directory inside another named one that copies at least as much is not copied
     twice. The snapshot is the whole of `<cache>/src/<name>/<id>` (`sourceRootOf`),
     even for a package configured from a subdirectory of it (zstd's and lz4's
     `build/cmake`, which name `build/cmake/../../lib`).
  4. Paths into the unit itself (the archives) become relative.
  5. What a nested child build takes from its dependencies (`Docs/Graph.md` §10): a
     path under a dependency's snapshot, `<cache>/src/<dep>/<id>` (its
     `<dep>_SOURCE_DIR`, when its step asked for the source), goes to the unit's
     `include/dep/<dep>/<relative path>`, and one under a dependency's entry,
     `<cache>/bin/<dep>/<platform>/<fp12>`, to its `dep/<dep>/<relative path>`. They
     are copied rather than rewritten into a reference to the dependency's own entry:
     the consumer may load that entry from elsewhere (another cache, a store), and an
     export file cannot name it before the dependency is loaded. A path under
     `<cache>/src` or `<cache>/bin` that is in no package's snapshot or entry fails the
     step, naming the property.
  What names a path decides how much of a directory is copied. A directory named by an
  include property (`INTERFACE_INCLUDE_DIRECTORIES`, `INTERFACE_SYSTEM_INCLUDE_DIRECTORIES`,
  a file set's `BASE_DIRS`) or an include option (`-I`, `-isystem`, `-iquote`,
  `-idirafter`) is copied minus what only a build reads or produces
  (`isCopiedIntoEntry`): hidden entries and `CMakeFiles` directories are not walked,
  and build scripts and project files (`CMakeLists.txt`, `*.cmake`, Makefiles, Ninja,
  Meson, Bazel files, logs, `.d` files), compiled output (objects, archives, libraries,
  PDBs, precompiled headers) and sources (`.c`, `.cpp`, `.m`, `.mm`, assembly, …) are
  left out. It is a rule of exclusion because headers need not have an extension
  (Eigen's do not). A directory named any other way (`INTERFACE_LINK_DIRECTORIES` over a
  vendored SDK's `lib/`, an ExternalProject's install directory, a `.xcframework` in
  `INTERFACE_LINK_LIBRARIES`, `-L`/`-F` options) is copied whole, but for
  version-control (`.git`, `.hg`, `.svn`) and `CMakeFiles` directories, so its
  libraries come along. Sources are kept in a directory the export also names a source
  file in (a module whose `INTERFACE_SOURCES` include their siblings), and a source
  file (`INTERFACE_SOURCES`, or a source by its extension) outside every directory the
  export names brings its own directory, with its sources, for what it includes by
  relative path; this is decided over every spelling of the tree before anything is
  copied. Any other referenced file is copied as is. A path that does not exist or
  climbs out of its tree fails the step, naming the property that holds it.
  A path is recognised where it starts: at the start of the text or after a quote,
  `;`, `:`, `=`, `,`, `>` or whitespace, or right after an option that takes it joined
  (`-I`, `-isystem`, `-iquote`, `-idirafter`, `-iframework`, `-isysroot`, `-include`,
  `-imacros`, `-L`, `-F`), so
  `/opt/epm` is not found inside `/usr/local/opt/epm`. It ends at a quote, `;`, a
  generator expression's `,` or `>`, a line end, and inside a `SHELL:` option at a
  space (`$<IF:c,<src>/a,<src>/b>`, `SHELL:-isystem <src>/x -DX`).
  Each tree is matched as written and with its symlinks resolved. The rewritten file is
  then checked, by the same rule: if it still names the cache root, the source
  snapshot, `<cache>/cpm`, the build tree, the unit or the staging directory, the step
  fails (`BuildError`, naming the path, its property and the line) and the build tree
  is kept; otherwise it is deleted. The archives' contents are not checked.
- **What each config carries.** Everything but `gen/shared` is in the config's unit,
  `include/` too, so a unit is self-contained, immutable and can be published (and
  later pushed or pulled) on its own; a config added later copies the headers it needs
  again rather than adding to a shared tree that published configs read. The cost is one
  copy of `include/` per config: for eacp (HelloGraph's: Release, graphics and WebView
  off) the unit carries the headers of its `Lib/`, `ThirdParty/miniz`,
  `ThirdParty/fmt/include` and five CPM dependencies, 4.8 MB beside 11 MB of Release
  archives (measured on macOS, Nov 2026), so a Debug + Release entry holds about 5 MB
  more than with one shared tree, beside Debug archives that are larger still.
- **Why `gen/shared` is content-addressed and outside the units.** Xcode fails a target
  whose sources vary by configuration, and the registration `.cpp` files in
  `INTERFACE_SOURCES` would otherwise be one path per config. Because the path is
  derived from the file's contents and lies outside every unit, every config that
  generates the same file names the same path, including a config added to the entry
  weeks later, without touching the configs already there. For eacp this shares the two
  `*_Register.cpp` files (300 bytes); the `.o` files and the generated include dirs stay
  per config. A shared file is never replaced nor deleted.
- **Publishing.** `CMakeBuild::harvest` publishes nothing. Once `cached`'s callback has
  returned (`run()`, then the `afterBuild` hook, which adds files to a config through
  `Prefix::configDirectory`), `cached` describes each config the callback left
  harvested (an export file with its `.names`; one left without fails the step):
  `describeStagedConfig` hashes every file of its unit and every `gen/shared` file its
  export names (sha256 and size), writes that list into the unit as `epm-files.json`
  and detects LTO objects (§9). Then
  `publishConfig`: (1) each of its `gen/shared` files the entry lacks is renamed into
  the entry (a file already there is kept: same path, same contents); (2) the marker,
  `epm-config.json`, recording the sha256 of `epm-files.json`, is written into the
  staged unit; (3) the unit is renamed to `configs/<Config>`, a rename that fails
  rather than replace a non-empty directory (on Windows a rename refused for a moment
  by a handle an antivirus or indexer holds, access denied or a sharing violation, is
  retried for about a second; a target that is there never is: `renameNoReplace`). A
  process that loses that race finds the winner's unit, marker and all, drops its own
  and goes on with the winner's (an identical build, by the fingerprint); so two
  processes, or two hosts sharing the cache, building the same config both end with a
  valid entry, without the package lock. The staging directory is then deleted. A
  config of LTO objects built by a compiler other than the one the entry was recorded
  for is published into the exact entry of the current fingerprint instead (§9). A
  callback that throws publishes nothing, so a first build that fails leaves an entry
  holding only `manifest.json` and the facade, which the next run picks (§9) and builds
  into; a failure in any config of a run (or in `afterBuild`) publishes none of that
  run's configs, the ones harvested before it included.
- **The facade**, `epm-targets.cmake`, is the same text in every entry
  (`targetsFileText`), written when the entry is created and never rewritten, so adding
  a config does not touch it. When it is loaded it finds the published configs (Debug,
  Release, RelWithDebInfo: those with a marker; with `EPM_FACADE_CONFIGS` set, as
  `epm_load_package` sets it to the response's `CONFIGS`, only those among them, so a
  config this consumer cannot link is never loaded), reads their `.names` and `.aliases`,
  adds `find_package(Threads REQUIRED)` if any export file mentions `Threads::Threads`,
  includes each export file, and defines the facade targets, choosing the selector for
  the set of configs found (one branch per set, from `configSelector`):

```cmake
foreach (_epm_config IN ITEMS Debug Release RelWithDebInfo)  # each published config
  … list(APPEND _epm_configs …) and its .names, .aliases …
endforeach ()
foreach (_epm_config IN LISTS _epm_configs)
  include("${CMAKE_CURRENT_LIST_DIR}/configs/${_epm_config}/export-${_epm_config}.cmake")
endforeach ()
foreach (_epm_target IN LISTS _epm_targets)                  # every exported library
  if (TARGET ${_epm_target})
    continue ()
  endif ()
  add_library(${_epm_target} INTERFACE IMPORTED GLOBAL)
  if (_epm_configs STREQUAL "Debug")                         # one branch per set
    …
  elseif (_epm_configs STREQUAL "Debug;Release")
    target_link_libraries(${_epm_target} INTERFACE
      "$<IF:$<CONFIG:Debug>,epm_debug::${_epm_target},epm_release::${_epm_target}>")
  elseif …
endforeach ()
foreach (_epm_line IN LISTS _epm_aliases)                    # "fmt::fmt fmt"
  … add_library(fmt::fmt ALIAS fmt) unless fmt::fmt exists …
endforeach ()
```

  The selector maps each consumer config to the same-named config if the entry has it,
  else Release, else RelWithDebInfo, else the first available, as nested
  `$<IF:$<CONFIG:…>,…,…>` expressions: `epm_release::${name}` alone for {Release}; for
  {Debug, Release, RelWithDebInfo}
  `$<IF:$<CONFIG:Debug>,epm_debug::…,$<IF:$<CONFIG:RelWithDebInfo>,epm_relwithdebinfo::…,epm_release::…>>`.
  So with {Release} only, a Debug consumer links the Release archives, except on the
  MSVC ABI, where the binary refuses the request instead (§9). The list of exported
  names is the union of the `.names` files. Known PoC limitation: the facade covers every
  exported name (including nested deps such as `Miro`, `fmt`); if the consumer defines one
  of those itself the guard keeps the consumer's target and the program may end up with
  two copies. To be addressed after the PoC. Because the facade finds configs when it is
  loaded, a consumer that configures again after another added a config to the entry
  sees it too, and its builds of that config link it instead of the fallback.

## 6. Repository layout (mirrors eacp / AgentWorkspace)

```
epm/
  CMakeLists.txt            CMake 3.31; options EPM_ENABLE_TESTS (default ON when top-level),
                            EPM_UNITY_BUILD (epm's own targets; default OFF),
                            EPM_UNITY_BUILD_DEPENDENCIES (eacp, Miro, ResEmbed;
                            default OFF), EPM_CI_BUILD (CI's flags: both of
                            those; default OFF);
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
    Cache/CacheVerify       `epm cache verify`
    Cache/CacheStore        cache stores: copying configs from them, `epm cache push`/`pull`
    Spec/Request, Env       Miro-reflected structs for the JSON files; Request's
                            `decide` is the mode decision
    Spec/Platform           the platform name (§9)
    Build/ChildBuild, Harvest   (+ EmbeddedScripts.cpp.in: epm-export.cmake compiled in)
    Build/Lto               LTO object detection in archives (§9)
    Build/Configs           config validation, mapping and the facade's selector
    Emit/Response
    Resolve/Resolve         the `epm resolve` pipeline (the default build steps, in-process)
  Apps/epm/Version.h.in     generated Version.h: epm::version from project(VERSION)
  Apps/epm/Main.cpp         Apps::runConsole + manual subcommand dispatch (configure,
                            graph-cmake, describe, recipe-sdk, resolve, cache path,
                            cache ls, cache verify, cache push, cache pull, --version);
                            configure compiles the root epm.cpp
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

Remote stores other than directories (HTTP, S3, release assets; directory stores are in
§3), store signing and garbage collection, offline mode, lockfile and prefetch, CPM shim for transitive deps, URL archives, patches,
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
- Every configure gets `-DEPM_ENABLE_TESTS=OFF`, `-DEPM_UNITY_BUILD=ON` and
  `-DEPM_UNITY_BUILD_DEPENDENCIES=ON` (epm's own targets and its dependencies as unity
  builds; an older tag ignores them) and, unless the environment sets
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
  configure stay: delete the entry to change them. `epm-configured` holds the settings
  get_epm passes (`-DEPM_UNITY_BUILD=ON;-DEPM_UNITY_BUILD_DEPENDENCIES=ON`); a
  directory configured with others, such as one from before an option, is configured
  again in place with them.
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

- `auto`: a single-config generator gets the consumer's `CMAKE_BUILD_TYPE` as it is
  built: Debug, Release or RelWithDebInfo (empty, `MinSizeRel` or any other type mean
  Release); a multi-config generator (Xcode, Visual Studio, Ninja Multi-Config) gets
  `Debug;Release`.
- An explicit list, e.g. `Release` or `Debug;Release`, of Debug, Release, RelWithDebInfo
  and MinSizeRel (any case; MinSizeRel is built as Release). Anything else stops the
  configure. `epm_resolve_configs` implements it (`Tests/EpmConfigsAuto.cmake`).

RelWithDebInfo is a configuration of its own, built with `CMAKE_BUILD_TYPE=RelWithDebInfo`
into its own `lib/RelWithDebInfo`, so a RelWithDebInfo consumer steps through its
dependencies with symbols; a consumer configuration the entry lacks links Release, else
RelWithDebInfo (`mapConsumerConfig`, the selector of §5). On the MSVC ABI a Debug build
compiles against the debug runtime (`_DEBUG`, `_ITERATOR_DEBUG_LEVEL`), whose objects do
not link with release archives, nor release objects with Debug ones: there the binary
refuses, at configure time, a prebuilt package whose configs give one of the consumer's
builds (its build type, or every type of a multi-config generator) a config of the other
kind (`configLinkProblem`), unless the consumer's `msvcRuntime` has no debug variant
(`MultiThreaded` alone gives a Debug build the release runtime). The fix it names: add
that config (a recipe's `releaseIfPossible()`, `Docs/Graph.md` §2, or `EPM_CONFIGS`), or
change the runtime.

Configs are **additive within an entry**: a Ninja Release configure builds only Release;
a later Ninja Debug configure finds that entry, builds only Debug and publishes it (its
own unit, `configs/Debug/`, without touching `configs/Release/` or the facade, §3), and
an Xcode configure after that needs nothing. A config already in an entry is never
rebuilt nor replaced.

**`EPM_ABI_MATCH`** (cache variable, default `compatible`, sent as `abiMatch`) says which
existing entries a request may use, comparing the entry's recorded fingerprint (§3) with
the current one:

| policy       | must be equal                                                        |
|--------------|----------------------------------------------------------------------|
| `strict`     | every recorded field (the whole canonical fingerprint)               |
| `compatible` | schema, platform, compilerId, compilerMajor, cCompilerId, the major version of cCompilerVersion, osxDeploymentTarget, the major version of sdkVersion (the NDK's major revision on Android), cxxStandard, androidApi, androidStl, msvcRuntime, toolchainFileHash, envFlags, options, sourceId, compatibleDeps |
| `any`        | schema, platform, msvcRuntime, options, sourceId, anyDeps            |

Each policy compares the dependencies as it compares everything else (`Docs/Graph.md`
§10): `compatibleDeps` and `anyDeps` hold each dependency entry's fingerprint projected
onto what that policy compares (`abiKey`), so a Mid entry built on CI against CI's Leaf
matches on a machine whose own Leaf entry is compatible with CI's; `strict` compares
`deps`, every hash, and still wants Mid built against this machine's exact Leaf. A
dependency recorded without a key for the policy is compared by its `deps` hash.

**LTO objects.** A config whose archives or objects hold link-time-optimisation IR
(LLVM bitcode, raw `BC 0xC0DE` or wrapped `0x0B17C0DE`, as eacp's Release builds with
Clang; GCC's `.gnu.lto_` sections in ELF and COFF objects, or its `__GNU_LTO` segment in
Mach-O ones; MSVC's `/GL` objects, anonymous COFF objects with the LTCG class ID) only
links with the compiler that wrote it. The harvest's publish step reads every file of
the unit (`ltoKindOf`: objects, every member of an ar archive in its GNU, BSD and
Windows spellings, every slice of a Mach-O universal file; a slice holds an archive or
an object and a member an object, nothing deeper, and a slice must lie after the
universal header and inside the file) and records `llvm-bitcode`, `gcc-lto` or
`msvc-ltcg` as the marker's `lto`, with the compiler
that built the config. Such a config is usable only by a consumer with the same
`compilerId` and full `compilerVersion` (`configUsableBy`), under **every** policy, `any`
included, since the link fails otherwise; that is stricter than `compatible`'s major
version, deliberately, as LLVM reads only bitcode no newer than itself and Apple's two
clangs (Xcode's and the command-line tools') often differ in their build number. An
entry holding a needed config this consumer cannot link is passed over, and so that the
compiler an entry was recorded for can always use it, LTO objects are only ever
published into an entry recorded for the compiler that built them: a missing config
built into a compatible entry of another compiler that turns out to hold LTO objects
goes to the current fingerprint's exact entry instead, which then gets the remaining
configs too. epm does not turn LTO off in child builds.

No policy looks at a path: none is in the fingerprint (§3), so even `strict` holds
across machines with the same toolchain. `compatible` ignores the compiler's minor and
patch version, the SDK's minor version and `systemProcessor` (the platform already
encodes the architecture), so Xcode's clang and the command-line tools' clang share
entries, and so do builds against macOS SDK 15.2 and 15.4: within one major version an
SDK adds API behind availability macros, and the deployment target, which is compared,
is what decides the symbols a library may use; a new major version is a new Xcode, whose
libc++ headers ship in the SDK. `envFlags` counts under `compatible` because any of those
flags can change the ABI (`-stdlib=`, `-D_GLIBCXX_DEBUG`, `-fno-exceptions`, `-m32`) and
epm cannot tell which do; `any` ignores them, as it ignores the compiler. When several entries qualify the one picked
(`pickEntry`) is: one holding any config at all before an empty one (what a failed build
leaves, §5), then one holding every needed config (usable by this compiler), then an
exact (strict) match, then the one holding most of them, then the lowest `fp12`. So a
complete compatible entry is used, by every mode, before an exact one that lacks a
config (which `binary-only` would otherwise fail on, `auto` without building fall back
to source over, and `prebuilt` rebuild), and when none is complete the exact entry is
picked, to build the missing configs into. No clock
takes part (`createdAt` differs between machines), so the same entries give the same
pick everywhere. With none, a new entry is created for the current fingerprint. Missing
configs are built into whichever entry was picked, with the current toolchain (the
config's marker records which), except LTO objects of a compiler the entry was not
recorded for (above). An entry picked for some configs may hold others this consumer
cannot link (LTO objects of another compiler): the response's `CONFIGS` leave them out
and the facade loads only the configs it names (`EPM_FACADE_CONFIGS`, §5), so they are
never linked. Using an entry that is not an exact match
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
