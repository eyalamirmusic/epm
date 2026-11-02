# epm proof of concept — scope and contract

Goal: a consumer project that depends on eacp, where epm keeps a **local** cache of eacp's
Debug and Release static libraries, and the consumer links against those libraries directly
when they exist. Nothing online (no remote stores, no lockfile, no signing) in this stage.

Everything here is PoC-level and may be replaced; the shape follows `PLAN.md`.

## 1. User-facing flow

```cmake
cmake_minimum_required(VERSION 3.31)
project(HelloEacp CXX)
include(path/to/epm/CMake/epm.cmake)        # finds the epm binary (EPM_EXECUTABLE or PATH)

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
- `MODE auto`: use the binary cache entry if present; otherwise build it (Debug + Release),
  then use it. (`EPM_BUILD_MISSING_BINARIES=OFF` makes `auto` fall back to source instead.)

Proof: configure the example twice in fresh build dirs. First run builds eacp into the cache
(slow). Second run is seconds, `add_subdirectory` never happens, and the linked archives live
under `~/.cache/epm/bin/eacp/<key>/lib/{Debug,Release}/`. `MODE source` still works.

## 2. Files exchanged between `epm.cmake` and the `epm` binary

All under `${CMAKE_BINARY_DIR}/epm/`.

### `env.json` (written once per configure by `epm.cmake`)

```json
{ "schema": 1,
  "cmake": "/path/to/cmake", "cmakeVersion": "3.31.6", "generator": "Ninja",
  "isMultiConfig": false,
  "buildType": "Debug", "configurationTypes": [],
  "cCompiler": "/usr/bin/cc", "cxxCompiler": "/usr/bin/c++",
  "cxxCompilerId": "AppleClang", "cxxCompilerVersion": "17.0.0.17000013",
  "systemName": "Darwin", "systemProcessor": "arm64",
  "osxArchitectures": "", "osxDeploymentTarget": "11.0",
  "cxxStandard": "20",
  "toolchainFile": "" }
```

Empty string / empty array when a variable is unset. Written at the first `epm_add_package`
call (not at include time), once per configure. `cCompiler` is empty when the consumer does
not enable C; the binary must not pass an empty `-DCMAKE_C_COMPILER` to child builds.

### `requests/<name>.json` (one per `epm_add_package` call)

```json
{ "schema": 1,
  "name": "eacp",
  "gitUrl": "https://github.com/eyalamirmusic/eacp.git",
  "gitRef": "main",
  "mode": "auto",
  "options": { "EACP_BUILD_GRAPHICS": "OFF", "EACP_ENABLE_TESTS": "OFF" },
  "buildMissingBinaries": true }
```

Invocation: `epm resolve --request <json> --env <json> --response <cmake> [--cache <dir>]
[--verbose]`. `--cache` is passed only when the CMake variable `EPM_CACHE` is set;
`EPM_VERBOSE` adds `--verbose` and streams the binary's output. Non-zero exit with a one-line
message on stderr means configure fails. `EPM_BUILD_MISSING_BINARIES` is a global `option()`
(default ON) whose value lands in `buildMissingBinaries`. `GIT_TAG` is required; `NAME`
defaults to the repository name as in CPM.

### `responses/<name>.cmake` (written by the binary, included by `epm.cmake`)

```cmake
set(epm_eacp_MODE "prebuilt")                                   # or "source"
set(epm_eacp_SOURCE_DIR "/Users/me/.cache/epm/src/eacp/3f1c0a9b2d4e")
set(epm_eacp_PREFIX "/Users/me/.cache/epm/bin/eacp/9a8b7c6d5e4f")  # prebuilt only
set(epm_eacp_TARGETS_FILE "${epm_eacp_PREFIX}/epm-targets.cmake")  # prebuilt only
```

`epm.cmake` then either `include()`s the targets file (prebuilt) or sets the OPTIONS as plain
variables and calls `add_subdirectory(<SOURCE_DIR> ${CMAKE_BINARY_DIR}/_deps/<name>-build)`
(source). In both cases it sets `<name>_SOURCE_DIR`, `<name>_ADDED` and `<name>_MODE` in the
caller's scope.

## 3. Cache layout (PoC subset of PLAN.md §5.3)

```
$EPM_CACHE  (default: <platform cache dir>/epm, e.g. ~/Library/Caches/epm or ~/.cache/epm)
  src/<name>/<sourceId12>/          clean snapshot, no .git
  src/<name>/<sourceId12>.json      completeness marker: {url, commit, fetchedAt}
  bin/<name>/<key12>/
      lib/Debug/*.a  lib/Release/*.a
      export-Debug.cmake export-Release.cmake   (CMake export() output, namespaced)
      epm-targets.cmake                          (facade targets, see §5)
      manifest.json                              (everything that went into the key)
  bin/<name>/<key12>.json           completeness marker, written last
  build/<name>/<key12>/<Config>/    child build trees; deleted after a successful harvest
  cpm/                              CPM_SOURCE_CACHE for child builds (nested CPM deps)
  tmp/                              staging; final locations are reached by rename
  locks/                            cross-process locks (eacp IPC::Lock), one per name
```

`sourceId = sha256("git\n" + url + "\n" + commit)`, first 12 hex chars as directory name.
`key = sha256(canonical JSON manifest)`, first 12 hex chars. Manifest fields (PoC):
`schema, name, sourceId, cxxCompiler, cxxCompilerId, cxxCompilerVersion, systemName,
systemProcessor, osxArchitectures, osxDeploymentTarget, cxxStandard, options (sorted),
configs ["Debug","Release"]`. Full toolchain probing (compiler content hash, macros) comes
later.

## 4. Source fetch

1. Resolve `gitRef` to a commit: 40 hex chars → as is; otherwise `git ls-remote <url>` and
   pick, in order, `refs/tags/<ref>^{}`, `refs/tags/<ref>`, `refs/heads/<ref>`.
2. Under the per-name lock, if the marker exists → done. Else into `tmp/<random>/`:
   `git init`, `git remote add origin <url>`, `git fetch --depth 1 origin <commit>`,
   `git checkout --detach FETCH_HEAD`, `git submodule update --init --depth 1` (if any),
   delete `.git`, rename into place, write the marker.
3. Shell out to the `git` CLI through `eacp::Processes` with a non-interactive environment
   (`GIT_TERMINAL_PROMPT=0`). Lift the wrapper from
   `/Users/eyalamir/Code/AgentWorkspace/Lib/WorkspaceCore/Git.{h,cpp}`.

## 5. Binary build and harvest

For each `Config` in `Debug`, `Release` (child build dir `build/<name>/<key>/<Config>`):

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

- Generator: the consumer's generator if it is single-config; otherwise `Ninja` if found on
  PATH, else `Unix Makefiles` (or the platform default on Windows).
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
  which the harvester reads instead of scraping the export file.
- Because archives are written straight into the prefix, the export file already points at
  cached `.a` files. Include directories point into the source cache (stable) and into
  `<cache>/cpm` for nested CPM dependencies (stable).
- **Relocation of build-tree paths.** Verified against eacp: the export file still references
  the child build tree in a few places — ResEmbed-generated include dirs and registration
  `.cpp` files in `INTERFACE_SOURCES` (Miro, eacp-webview-bridge), and `IMPORTED_OBJECTS` of
  OBJECT libraries (MiroFormats, MiroCodegenWriter). The harvester therefore, per config:
  scans the export file for every path that starts with `<builddir>`, copies each referenced
  file or directory to `<prefix>/gen/<Config>/<path relative to builddir>`, and replaces the
  literal `<builddir>` prefix with `<prefix>/gen/<Config>` throughout the export file. After
  that no path references the build tree and the build dir is deleted. If any reference
  survives the rewrite (unexpected), warn and keep the build dir.
- If any export file mentions `Threads::Threads`, `epm-targets.cmake` adds
  `find_package(Threads REQUIRED)` before the includes.
- Generate `epm-targets.cmake`:

```cmake
include("${CMAKE_CURRENT_LIST_DIR}/export-Debug.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/export-Release.cmake")
foreach(name IN ITEMS eacp-core eacp-network …)           # every exported library
  if(NOT TARGET ${name})
    add_library(${name} INTERFACE IMPORTED)
    target_link_libraries(${name} INTERFACE
      "$<IF:$<CONFIG:Debug>,epm_debug::${name},epm_release::${name}>")
  endif()
endforeach()
```

  The list of exported names comes from the `.names` files. Facade targets are
  `IMPORTED GLOBAL`. RelWithDebInfo/MinSizeRel consumers therefore use Release.
  Known PoC limitation: the facade covers every exported name (including nested deps such
  as `Miro`, `fmt`); if the consumer defines one of those itself the guard keeps the
  consumer's target and the program may end up with two copies. To be addressed after the PoC.
- Write `manifest.json`, then the completeness marker. Only then is the entry visible.

## 6. Repository layout (mirrors eacp / AgentWorkspace)

```
epm/
  CMakeLists.txt            CMake 3.31; options EPM_ENABLE_TESTS (default ON when top-level)
  .clang-format .gitignore  copied from /Users/eyalamir/Code/eacp
  CLAUDE.md                 conventions pointer (eacp style, NanoTest, Miro for JSON)
  CMake/CPM.cmake           vendored 0.40.2 (same as other projects), for epm's own deps
  CMake/Findeacp.cmake, FindMiro.cmake, FindNanoTest.cmake
  CMake/epm.cmake           consumer module (the file users include)
  CMake/epm-export.cmake    injected into child builds (see §5)
  Lib/epm/                  static library `epm-core`, namespace `epm::`
    Util/Sha256.{h,cpp}     lifted from /Users/eyalamir/Code/PackagesCPP/Lib/pkgcpp/Sha256.*
    Util/Git.{h,cpp}        lifted from AgentWorkspace
    Cache/CacheRoot, SourceCache, BinaryCache
    Spec/Request, Env       Miro-reflected structs for the JSON files
    Build/ChildBuild, Harvest
    Emit/Response
  Apps/epm/Main.cpp         Apps::runConsole + manual subcommand dispatch (resolve, --version)
  Tests/                    NanoTest: Sha256 vectors, key canonicalisation, response text,
                            export-file name scraping, ref resolution parsing
  Examples/HelloEacp/       the consumer project from §1
```

eacp is consumed with `EACP_BUILD_GRAPHICS OFF`, `EACP_BUILD_WEBVIEW OFF`,
`EACP_ENABLE_TESTS OFF`, `EACP_ENABLE_EXAMPLES OFF`; link `eacp-core` only.

## 7. Out of scope for the PoC

Remote stores, lockfile and prefetch, CPM shim for transitive deps, URL archives, patches,
case-insensitive name matching, compiler content hashing, Windows/Linux validation (code
should stay portable, but only macOS is exercised), bootstrapping the binary from CMake
(`EPM_EXECUTABLE` or PATH is required).
