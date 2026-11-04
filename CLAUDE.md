# CLAUDE.md

## Git rules

Never commit, push, tag, or create releases unless the user explicitly asks for
it in the current request. This applies to subagents too: every agent brief must
say "do not commit or push". Leave changes in the working tree (or on the
current branch, uncommitted) and report them; the user decides when they land.

epm is a C++ package manager for CMake projects, built on the eacp framework
(`eacp-core` only). `README.md` describes what it does; `PLAN.md` is the
longer-term design and background.

## Layout

- `Lib/epm/` — static library `epm-core`, namespace `epm::`. Included as
  `<epm/Area/File.h>`.
  - `Util/` — `Sha256`, `Git` (git CLI through `eacp::Processes`, never
    interactive, always run from the temp directory), `LongPath`, `Status`,
    `Command` (child processes, `findExecutable`), `Log` (`--verbose`), `Text`,
    `Timestamp` (durations).
  - `Spec/` — `Env`, `Request`, `Manifest` (`epm.json`: packages, profiles and
    the root's `overrides`), `Overrides` (`overrides.json`, filled from the
    environment): Miro-reflected structs whose field names are the JSON keys;
    `JsonFile.h` loads and saves them. `Platform` names the target platform
    from `Env`. `PackageArguments` is the one parser of CPMAddPackage /
    FetchContent_Declare / epm_add_package arguments, plus epm's `MODE`.
  - `Cache/` — `CacheRoot`, `BinaryCache` (entries under
    `bin/<name>/<platform>/<fp12>`, picking, additive config builds, configs
    invalidated when the lock changes what they were built against),
    `Fingerprint` (the ABI fingerprint, keyed on the Merkle hash, and the
    `EPM_ABI_MATCH` policies), `CacheListing` (`epm cache ls`), `CrossProcessLock`
    (one package's or URL's lock across processes: eacp `IPC::Lock`, hence
    the `eacp-network` dependency).
  - `Source/` — `SourceCache` (bare clones under `git/`, archives under
    `downloads/`, trees under `src/<merkle hash>`), `BareClone` (git plumbing,
    git-lfs), `Submodules`, `Download`, `SourceSnapshot` (a cached tree, and
    `isFullCommitHash`). Ported from lockstep.
  - `Lock/` — `LockFile` (`epm-lock.json`), `MerkleHash`, `Requirements`
    (minimal version selection), `Session`
    (`<build>/epm/session.json`), `ErrorMessages`. Ported from lockstep.
  - `Build/` — `ChildBuild`, `Harvest`, `Configs` (config mapping and the
    facade's `$<CONFIG>` selector). `CMake/epm-export.cmake`, `CMake/epm.cmake`
    and `CMake/epm-provider.cmake` are compiled into the binary through
    `Build/EmbeddedScripts.cpp.in` and written into every child build, so a
    dependency's own package calls go through the consumer's lock.
  - `Emit/` — `Response` (the CMake a package call runs for a prebuilt
    entry), `GraphResponse` (`graph.cmake` and `graph.json`).
  - `Graph/` — `Merge` (one package's effective request and its provenance),
    `Graph` (the manifest walk, notes, `dependencyClosure`, resolution in
    dependency order: `epm configure`; git and tag are selected through the
    lock), `ResolvedGraph` (`graph.json`).
  - `Resolve/` — `Commands` (resolve, provide, configure and the `lock`
    subcommands: overrides, lock modes, minimal version selection, the CMake
    each call runs) and `Resolve` (the mode decision and the binary half).
- `Apps/epm/Main.cpp` — the `epm` executable (`configure`, `graph-cmake`,
  `resolve`, `provide`, `lock`, `cache`): `Apps::runConsole` plus manual
  subcommand dispatch on `argv[1]`, `Args::Parser` per subcommand.
- `epm.json`, `epm-lock.json` — epm's own dependencies (eacp, Miro,
  NanoTest), resolved by the released epm `CMakeLists.txt` pins.
- `CMake/` — `get_epm.cmake` (the file consumers vendor: finds
  the binary, then registers the provider when there is an `epm.json`),
  `epm.cmake` (the consumer module, installed with the binary),
  `epm-provider.cmake` (the dependency provider and `epm_load_package`,
  installed beside `epm.cmake`) and `epm-export.cmake` (injected into child
  builds).
- `Tests/` — NanoTest, one `<Area>Tests.cpp` per area, plus `cmake -P`
  self-tests: `GetEpmRanges.cmake` (get_epm's `EPM_VERSION` range handling,
  ctest `get_epm_ranges`), `EpmConfigsAuto.cmake` (epm.cmake's
  `EPM_CONFIGS` resolution, ctest `epm_configs_auto`), `EpmOverrides.cmake`
  (the `overrides.json` writer, ctest `epm_overrides`), `GraphProvider.cmake`
  (the provider over fixtures in `Tests/Graph/`, ctest `epm_graph_provider`)
  and `GraphNested.cmake` (ctest `epm_graph_nested`: the real binary over two
  local git repositories from `Tests/Nested/`, a package and its dependency,
  prebuilt and from source; skipped without git).
- `Examples/` — consumer projects: `HelloEacp` (`epm_add_package`) and
  `HelloGraph` (the same through `epm.json` and `find_package`).

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`cmake --install build --prefix <dir>` installs only `bin/epm`,
`share/epm/cmake/epm.cmake` and `share/epm/cmake/epm-provider.cmake`. Consumers vendor `CMake/get_epm.cmake`, which uses
an in-range epm on PATH, else downloads the latest release (its
`manifest.json`) into `<cache>/tool/<tag>/`, or builds it there through those
same install rules, then includes that entry's `epm.cmake`.
`CMake/epm.cmake` requires `EPM_EXECUTABLE` and has no bootstrap of its own.
A `get_epm.cmake` inside an epm checkout (`<dir>/CMake/`, `<dir>/CMakeLists.txt`
with `project(epm`) defaults `EPM_SOURCE_DIR` to that checkout, so the examples,
which list `../../CMake/get_epm.cmake`, build epm from this checkout (an
incremental build into `<cache>/tool/local-<hash>/`); `-DEPM_SOURCE_DIR` or
`-DEPM_EXECUTABLE` overrides it. Examples hold only what a real consumer writes.

One version: `project(epm VERSION ...)` (`Apps/epm/Version.h.in` carries it
into `epm --version`, release.yml names the assets after it and checks it
against a `v*` tag). No version or hash is kept anywhere else, and CI never
commits. To release: bump `project(VERSION)`, merge to main, tag `vX.Y.Z` and
push the tag; release.yml publishes the archives, `SHA256SUMS`,
`manifest.json` and `get_epm.cmake`. Never move or delete a published tag.

`EPM_ENABLE_TESTS` defaults ON when epm is the top-level project.
`EPM_VERBOSE_CONFIGURE=ON` brings back the configure detail hidden by default.
Use a local eacp with `-DEPM_eacp_SOURCE=/path/to/eacp`.

## Code style

Follow eacp's conventions (`/Users/eyalamir/Code/eacp/CLAUDE.md`, Code Style):

- Before writing any helper, check eacp's `Docs/Toolbox.md`. If eacp lacks it
  and it is general, note it for upstreaming rather than growing a local copy.
- Modern C++20 and RAII. `auto` for locals; never for function return types.
- PascalCase types, camelCase functions and variables, `xToUse` for
  constructor parameters that shadow members.
- Few comments: name functions so the code explains itself; comment only the
  why that the code cannot say.
- Files via `eacp::Files::` / `FilePath`, processes via `eacp::Processes`,
  arguments via `eacp::Args`, environment via `eacp::getEnv`. Prefer the
  bool-returning `Files::` helpers and return bool/optional from epm's own
  fallible functions; `writeFile`/`writeFileAtomically` throw, so catch at the
  boundary (see `Spec/JsonFile.h`).
- All JSON goes through Miro reflection (`MIRO_REFLECT`, `Miro::toJSONString`,
  `Miro::createFromJSON`); never hand-walk JSON values.
- Tests: `auto tName = test("Area/name") = [] { check(...); };`. Test
  variables have external linkage, so names must be unique across files.
- `.clang-format` (copied from eacp) is enforced: Allman braces, 85 columns,
  4-space indent, left pointer alignment. Run clang-format on every edited file.
