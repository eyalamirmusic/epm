# CLAUDE.md

epm is a C++ package manager for CMake projects, built on the eacp framework
(`eacp-core` only). `Docs/PoC.md` is the current contract — scope, the JSON and
CMake files exchanged with `CMake/epm.cmake`, the cache layout, and the
repository layout. `PLAN.md` is the longer-term design and background.

## Layout

- `Lib/epm/` — static library `epm-core`, namespace `epm::`. Included as
  `<epm/Area/File.h>`.
  - `Util/` — `Sha256`, `Git` (git CLI through `eacp::Processes`, never
    interactive, always run from the temp directory), `LongPath`, `Status`, `Command` (child processes, `findExecutable`), `Log`
    (`--verbose`), `Text`.
  - `Spec/` — `Env`, `Request`: Miro-reflected structs whose field names are the
    JSON keys; `JsonFile.h` loads and saves them.
  - `Cache/` — `CacheRoot`, `SourceCache` (`SourceSnapshot`,
    `isFullCommitHash`), `BinaryCache`, `PackageLock` (eacp `IPC::Lock`,
    hence the `eacp-network` dependency).
  - `Source/` — `PackageCache` (bare mirrors under `git/`, archives under
    `downloads/`, trees under `src/<merkle hash>`), `Mirror` (git plumbing,
    git-lfs), `Submodules`, `Download`. Ported from lockstep.
  - `Lock/` — `LockFile` (`epm.lock`), `MerkleHash`, `Session`
    (`<build>/epm/session.json`), `Messages`. Ported from lockstep.
  - `Build/` — `ChildBuild`, `Harvest`. `CMake/epm-export.cmake` is compiled
    into the binary through `Build/EmbeddedExport.cpp.in`.
  - `Emit/` — `Response` (the `responses/<name>.cmake` writer).
  - `Resolve/` — `Commands` (resolve, provide and the `lock` subcommands:
    overrides, lock modes, the CMake each call runs) and `Resolve` (the mode
    decision and the binary half).
  - `Spec/PackageArguments` — the one parser of CPMAddPackage /
    FetchContent_Declare / epm_add_package arguments, plus epm's `MODE`.
- `Apps/epm/Main.cpp` — the `epm` executable: `Apps::runConsole` plus manual
  subcommand dispatch on `argv[1]`, `Args::Parser` per subcommand.
- `CMake/` — vendored `CPM.cmake`, `Find<dep>.cmake` shims (one
  `CPMAddPackage` each), `get_epm.cmake` (the file consumers vendor: finds
  the binary), `epm.cmake` (the consumer module, installed with the binary)
  and `epm-export.cmake` (injected into child builds).
- `Tests/` — NanoTest, one `<Area>Tests.cpp` per area, plus
  `GetEpmRanges.cmake`, a `cmake -P` self-test of get_epm's `EPM_VERSION`
  range handling (ctest `get_epm_ranges`).
- `Examples/` — consumer projects that exercise `epm.cmake`.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`cmake --install build --prefix <dir>` installs only `bin/epm` and
`share/epm/cmake/epm.cmake`. Consumers vendor `CMake/get_epm.cmake`, which uses
an in-range epm on PATH, else downloads the latest release (its
`manifest.json`) into `<cache>/tool/<tag>/`, or builds it there through those
same install rules, then includes that entry's `epm.cmake` (`Docs/PoC.md` §8).
`CMake/epm.cmake` requires `EPM_EXECUTABLE` and has no bootstrap of its own.

One version: `project(epm VERSION ...)` (`Apps/epm/Version.h.in` carries it
into `epm --version`, release.yml names the assets after it and checks it
against a `v*` tag). No version or hash is kept anywhere else, and CI never
commits. To release: bump `project(VERSION)`, merge to main, tag `vX.Y.Z` and
push the tag; release.yml publishes the archives, `SHA256SUMS`,
`manifest.json` and `get_epm.cmake`. Never move or delete a published tag.

`EPM_ENABLE_TESTS` defaults ON when epm is the top-level project.
`EPM_VERBOSE_CONFIGURE=ON` brings back the configure detail hidden by default.
Use a local eacp with `-DCPM_eacp_SOURCE=/path/to/eacp`.

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
