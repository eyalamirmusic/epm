# epm proof of concept — scope and contract

> The per-call `epm_add_package` flow of §1–§3 is superseded by `Docs/Graph.md` (an
> `epm.json` manifest, CMake's dependency provider, `epm configure`) and kept for
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
include(cmake/get_epm.cmake)                 # finds or fetches epm, includes its module (§8)

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
  "systemName": "Darwin", "systemProcessor": "arm64",
  "osxArchitectures": "", "osxDeploymentTarget": "11.0",
  "osxSysroot": "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk",
  "androidAbi": "",
  "cxxStandard": "20",
  "toolchainFile": "" }
```

`osxSysroot` is `CMAKE_OSX_SYSROOT` and `androidAbi` is `CMAKE_ANDROID_ARCH_ABI` (else the
NDK toolchain's `ANDROID_ABI`); both only feed the platform name (§9).
Empty string / empty array when a variable is unset. Written at the first `epm_add_package`
call (not at include time), once per configure. `cCompiler` is empty when the consumer does
not enable C; the binary must not pass an empty `-DCMAKE_C_COMPILER` to child builds.
`makeProgram` (`CMAKE_MAKE_PROGRAM`) is passed to a child build that reuses the consumer's
generator, so a build tool that is not on PATH (CLion's bundled ninja) is still found.

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
  src/<merkle hash>/                clean tree, no .git, patches applied
  src/<merkle hash>.stamp           size/mtime fingerprint, so a hit is not rehashed
  src/<merkle hash>.symlinks        the tree's declared symlinks, for a rehash
  git/<sha256(url)[:16]>.git        bare mirror, one per URL (submodules too)
  downloads/<sha256(url)[:16]>/     downloaded archives
  bin/<name>/<platform>/<fp12>/     one entry per ABI fingerprint (§9)
      manifest.json                 {schema, fingerprint, sourceHash, createdAt}; written first
      <Config>.json                 per-config completeness marker, written last for that
                                    config: {config, builtAt, compilerPath, compilerVersion,
                                    lockedDependencies}; a config whose lockedDependencies
                                    the lock no longer holds is rebuilt
      lib/<Config>/*.a|*.lib
      export-<Config>.cmake (+ .names, .aliases)   CMake export() output, namespaced epm_<config>::
      gen/<Config>/…                relocated per-config build-tree paths (include dirs, objects)
      gen/shared/<sha12>/<relpath>  relocated INTERFACE_SOURCES files, content-addressed
      epm-targets.cmake             facade targets (§5), regenerated when a config is added
  build/<name>/<platform>/<fp12>/<Config>/   child build trees; deleted after the harvest
  cpm/                              CPM_SOURCE_CACHE for child builds (nested CPM deps)
  tool/<ref>/bin/epm                a bootstrapped binary (§8), an epm install prefix
  tool/<ref>/share/epm/cmake/epm.cmake   the module that goes with it
  tool/<ref>.json                   its completeness marker: {ref, origin, url, version, builtAt}
  tool/<ref>.lock                   CMake file(LOCK) held while bootstrapping
  tool/latest.json                  the latest release's manifest.json, plus checkedAt
  tmp/                              staging; final locations are reached by rename
```

Locks: one eacp `IPC::Lock` per package name, held while fetching a snapshot and while
building an entry (waits up to an hour for another process). `IPC::Lock` keeps its lock
files in its own per-user directory (`<appData>/eacp/Locks`) and takes no path, so the lock
is named `epm-<sha256(cache root)[:12]>-<name>` to keep separate caches independent; there
is no `locks/` directory. The cache root is resolved (`weakly_canonical`) before use so
every path epm writes and matches has one spelling.

A tree is stored under its Merkle hash: each directory hashes `"<file|directory|symlink> <hash> <name>\n"`
for its entries sorted by name, `.git` excluded, execute bits ignored.
`fp12` is the first 12 hex chars of `sha256(canonical fingerprint JSON)`. The fingerprint
(`AbiFingerprint`, recorded in `manifest.json`) holds `schema` (3), `platform`,
`compilerId`, `compilerVersion` (full), `compilerMajor`, `compilerPath`, `systemName`,
`systemProcessor`, `osxArchitectures`, `osxDeploymentTarget`, `cxxStandard`,
`toolchainFile` and `toolchainFileHash` (sha256 of its contents, when there is one),
`options` (sorted), `sourceHash` (the Merkle hash the lock records) and `deps`. The canonical form is Miro's compact output of the
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

Under the package lock the entry is picked again (another process may have finished it) or
created (`manifest.json` first). Then, for each `Config` the request needs that the entry
lacks a marker for, and only those (child build dir
`build/<name>/<platform>/<fp12>/<Config>`; whatever an unmarked config left in `lib/`,
`gen/` or its export files is removed first):

```
cmake -S <src> -B <builddir> -G <generator>
      -DCMAKE_BUILD_TYPE=<Config>
      -DCMAKE_C_COMPILER=… -DCMAKE_CXX_COMPILER=…
      -DCMAKE_OSX_ARCHITECTURES=… -DCMAKE_OSX_DEPLOYMENT_TARGET=… -DCMAKE_CXX_STANDARD=…
      -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=<prefix>/lib/<Config>
      -DCPM_SOURCE_CACHE=<cache>/cpm
      -DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<builddir>/epm-export.cmake
      -DEPM_EXPORT_FILE=<prefix>/export-<Config>.cmake
      -DEPM_EXPORT_NAMESPACE=epm_<config>::          # epm_debug:: / epm_release::
      -D<OPTION>=<VALUE> …                           # from the request
cmake --build <builddir> --parallel
```

- Generator: the consumer's generator (and `CMAKE_MAKE_PROGRAM`) if it is single-config;
  otherwise `Ninja` if found on PATH (passed as `CMAKE_MAKE_PROGRAM`), else
  `Unix Makefiles` (or the platform default on Windows). `-D` flags whose env value is empty
  are left out; `-DCMAKE_TOOLCHAIN_FILE` is passed when the consumer has one.
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
- Write `<Config>.json` (the config is now in the entry), then regenerate
  `epm-targets.cmake` for the configs whose markers exist. If any export file mentions
  `Threads::Threads`, it adds `find_package(Threads REQUIRED)` before the includes:

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
  files may still be cleared; a failure in a later config leaves the earlier ones usable.

## 6. Repository layout (mirrors eacp / AgentWorkspace)

```
epm/
  CMakeLists.txt            CMake 3.31; options EPM_ENABLE_TESTS (default ON when top-level);
                            install: bin/epm, share/epm/cmake/epm.cmake
  README.md
  .clang-format .gitignore  copied from /Users/eyalamir/Code/eacp
  CLAUDE.md                 conventions pointer (eacp style, NanoTest, Miro for JSON)
  CMake/CPM.cmake           vendored 0.40.2 (same as other projects), for epm's own deps
  CMake/Findeacp.cmake, FindMiro.cmake, FindNanoTest.cmake
  CMake/get_epm.cmake       the file users vendor and include: finds the binary (§8)
  CMake/epm.cmake           consumer module (epm_add_package), installed with the binary
  CMake/epm-export.cmake    injected into child builds (see §5)
  Lib/epm/                  static library `epm-core`, namespace `epm::`
    Util/Sha256.{h,cpp}     lifted from /Users/eyalamir/Code/PackagesCPP/Lib/pkgcpp/Sha256.*
    Util/Git.{h,cpp}        lifted from AgentWorkspace
    Util/Command, Log, Text, Timestamp   child processes, findExecutable, --verbose, text
    Cache/CacheRoot, SourceCache, BinaryCache, PackageLock
    Cache/Fingerprint       AbiFingerprint, EPM_ABI_MATCH policies, the configure note
    Cache/CacheListing      `epm cache ls`
    Spec/Request, Env       Miro-reflected structs for the JSON files
    Spec/Platform           the platform name (§9)
    Build/ChildBuild, Harvest   (+ EmbeddedScripts.cpp.in: epm-export.cmake compiled in)
    Build/Configs           config validation, mapping and the facade's selector
    Emit/Response
    Resolve/Resolve         the pipeline and the mode decision
  Apps/epm/Version.h.in     generated Version.h: epm::version from project(VERSION)
  Apps/epm/Main.cpp         Apps::runConsole + manual subcommand dispatch (resolve,
                            cache path, cache ls, --version)
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

### Release manifest

The publish job of release.yml writes `manifest.json` next to the archives and
`SHA256SUMS` (`.github/scripts/make-manifest.cmake`):

```json
{ "schema": 1, "version": "0.1.2", "tag": "v0.1.2",
  "assets": { "macos-universal": { "file": "epm-0.1.2-macos-universal.tar.gz",
                                   "sha256": "<hex>" },
              "linux-x64": { … }, "linux-arm64": { … },
              "windows-x64": { "file": "epm-0.1.2-windows-x64.zip", … },
              "windows-arm64": { … } } }
```

get_epm reads it from `https://github.com/eyalamirmusic/epm/releases/latest/download/manifest.json`,
a plain download that follows GitHub's redirect to the newest non-prerelease release; the
GitHub API is not used (unauthenticated it is rate-limited). The rolling `nightly`
prerelease carries a manifest too (tag `nightly`), but `releases/latest` never points at a
prerelease, so get_epm never picks it up.

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
     source directory), when set, skipping 2–5: an epm checkout (a directory whose
     `CMakeLists.txt` has `project(epm`, else configure stops), built and installed with
     `--prefix <cache>/tool/local-<sha256(real path)[:12]>` on every configure, under
     that entry's lock. The build directory stays in the entry, one per generator
     (`build-ninja/`, …), so after the first build it is an incremental `cmake --build`
     plus `cmake --install`. Release build, `EPM_ENABLE_TESTS=OFF`, through the same
     clean environment as step 5 but with the consumer's generator and make program, and
     its C/C++ compilers when known and not possibly a cross toolchain's (no toolchain
     file, `CMAKE_SYSTEM_NAME` the host's); as a top-level include get_epm runs before
     languages are enabled, so usually none. Only the first configure of a build
     directory passes them; delete the entry to change compilers. The entry's `bin/epm` and `share/epm/cmake/epm.cmake` are used and
     the marker rewritten; the version is printed (`-- epm: using epm 0.1.3 built from
     …`), never checked against `EPM_VERSION`. For testing an unreleased epm from a
     consumer. `EPM_BOOTSTRAP_SOURCE_DIR`, the old name, is read as `EPM_SOURCE_DIR`
     when neither the variable nor the environment variable is set, with
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
3. The newest in-range entry `<cache>/tool/v*/` with its marker, *unless* the latest
   release (step 4's manifest) is newer and also in range: without that exception a
   machine would stay on the first version it ever downloaded. Reading the manifest here
   costs no network within the 24 hours below.
4. A release. The latest manifest is cached as `<cache>/tool/latest.json`
   (`{schema, checkedAt: <unix seconds>, manifest}`) and re-fetched only when older than
   24 hours or when `EPM_UPDATE` is ON (a one-off: get_epm removes it from the cache
   after the configure); if the fetch fails (offline) the stale copy is
   used, and with no copy the step is skipped. If the latest version is in range it is
   used; otherwise, if `EPM_VERSION` has an exact lower bound `x.y.z`,
   `releases/download/vx.y.z/manifest.json` is tried the same way. Those are the only two
   releases ever looked at. `assets[<platform>].file` is downloaded from
   `releases/download/<tag>/` (`TLS_VERIFY ON`) into `<cache>/tmp/`, its `file(SHA256)`
   compared with the manifest (by hand, not `EXPECTED_HASH`, whose mismatch would end the
   configure), and it is extracted into `<cache>/tool/<tag>/`. `bin/epm --version` must
   print the manifest's version, which catches a binary the host cannot run (too old a
   glibc, no libcurl, no Visual C++ runtime). The marker is written last, with
   `"origin": "release"`. `-- epm: downloaded epm 0.1.2 (macos-universal) into …/tool/v0.1.2`.
   Any failure removes the partial files, prints `-- epm: release download failed
   (<reason>), building from source` and goes on. `EPM_BOOTSTRAP_NO_DOWNLOAD=ON` skips
   the download (the manifest still chooses the version to build).
5. A source build of the chosen version: the release from step 4, else `v<exact lower
   bound>`, else `main`. GitHub's tarball of that ref is downloaded into `<cache>/tmp/`,
   configured, built and `cmake --install`ed with `CMAKE_INSTALL_PREFIX=<cache>/tool/<ref>`.
   The build is a host Release build: it runs through `cmake -E env --unset=…` so no
   toolchain file, generator, build type or `CMAKE_OSX_ARCHITECTURES` of the consumer
   (variable or environment) reaches it; a consumer targeting iOS or Android still gets a
   macOS/Linux/Windows `epm`. A failed build is fatal, with the end of its output and the
   log path (`<cache>/tool/<ref>/build/epm-bootstrap.log`, kept). `-- epm: building epm
   v0.1.2 from source into … (first time only)`; `EPM_VERBOSE` streams the build. The
   marker carries `"origin": "source"`. If the source cannot be downloaded either, an
   in-range cached entry is used if there is one, else configure stops.

Steps 4 and 5 run under `file(LOCK <cache>/tool/<key>.lock)`; a configure that waited on
the lock finds the marker the first one wrote. A release download and a source build of
the same version share the entry `tool/vX.Y.Z`.

Every tool entry, whether unpacked from a release archive, built from source, or an epm
installed on PATH, is the layout `cmake --install` produces: `bin/epm[.exe]` and
`share/epm/cmake/epm.cmake`. get_epm finally sets `EPM_EXECUTABLE` (cache `INTERNAL`) and
includes `<entry>/share/epm/cmake/epm.cmake`. The pick is also remembered as
`EPM_SELECTED_EXECUTABLE`, so the next configure picks afresh (a newer release, a changed
range, a new PATH install) instead of mistaking it for a user-given `EPM_EXECUTABLE`.
The same goes for a path the pre-get_epm `epm.cmake` bootstrapped
(`EPM_BOOTSTRAPPED_EXECUTABLE`, removed from the cache on the first get_epm configure).

The marker `<cache>/tool/<key>.json` holds `schema`, `ref`, `origin` (`release` or
`source`), `url` (the asset, tarball or checkout it came from), `version` and `builtAt`.

Source overrides (each skips steps 2–4): `EPM_SOURCE_REF` (tag, branch or commit; entry
`tool/<ref>`) and `EPM_SOURCE_URL` (a tarball; entry `<ref or "url">-<sha256(url)[:12]>`);
a local checkout is `EPM_SOURCE_DIR` (step 1). Entries for branches (`main`) are not
refreshed. Refs are sanitised to `[A-Za-z0-9._-]` for the directory name.

The cache root is the one the binary uses: the `EPM_CACHE` CMake variable (passed as
`--cache`), else the `EPM_CACHE` environment variable, else eacp's
`FilePath::cacheDirectory()` + `/epm` mirrored in CMake: `$HOME/Library/Caches` on macOS,
`$XDG_CACHE_HOME` or `$HOME/.cache` on Linux, `%LOCALAPPDATA%` on Windows.

`cmake --install` of epm installs exactly `bin/epm` and `share/epm/cmake/epm.cmake`;
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
is: an exact (strict) match first, then one holding every needed config, then the one
holding most of them, then the newest. With none, a new entry is created for the current
fingerprint. Missing configs are built into whichever entry was picked, with the current
toolchain (its `<Config>.json` records which). Using an entry that is not an exact match
adds a note to the response and the status line:
`-- epm: eacp (prebuilt, macos-universal, 1a2b3c4d5e6f, built with AppleClang
17.0.0.17000013, current 17.0.0.17000319)`.

**Platform names** come from `env.json` (`platformName`, `Spec/Platform`):
`macos-universal` (`CMAKE_OSX_ARCHITECTURES` holds both arm64 and x86_64), `macos-arm64`,
`macos-x64`; `ios-arm64`, and `ios-simulator-arm64` / `ios-simulator-x64` when
`CMAKE_OSX_SYSROOT` contains "simulator" (any case); `windows-x64`, `windows-arm64`,
`linux-x64`, `linux-arm64` (x86_64/AMD64 → x64, aarch64 → arm64); `android-<abi>`;
anything else `<system name>-<processor>` in lower case. The five desktop names are the
release asset keys `get_epm.cmake` uses.
