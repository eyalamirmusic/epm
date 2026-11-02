# CLAUDE.md

epm is a C++ package manager for CMake projects, built on the eacp framework
(`eacp-core` only). `Docs/PoC.md` is the current contract — scope, the JSON and
CMake files exchanged with `CMake/epm.cmake`, the cache layout, and the
repository layout. `PLAN.md` is the longer-term design and background.

## Layout

- `Lib/epm/` — static library `epm-core`, namespace `epm::`. Included as
  `<epm/Area/File.h>`.
  - `Util/` — `Sha256`, `Git` (git CLI through `eacp::Processes`, never
    interactive), `Command` (child processes, `findExecutable`), `Log`
    (`--verbose`), `Text`, `Timestamp`.
  - `Spec/` — `Env`, `Request`: Miro-reflected structs whose field names are the
    JSON keys; `JsonFile.h` loads and saves them.
  - `Cache/` — `CacheRoot`, `SourceCache`, `BinaryCache`, `PackageLock`
    (eacp `IPC::Lock`, hence the `eacp-network` dependency).
  - `Build/` — `ChildBuild`, `Harvest`. `CMake/epm-export.cmake` is compiled
    into the binary through `Build/EmbeddedExport.cpp.in`.
  - `Emit/` — `Response` (the `responses/<name>.cmake` writer).
  - `Resolve/` — the `epm resolve` pipeline and the mode decision.
- `Apps/epm/Main.cpp` — the `epm` executable: `Apps::runConsole` plus manual
  subcommand dispatch on `argv[1]`, `Args::Parser` per subcommand.
- `CMake/` — vendored `CPM.cmake`, `Find<dep>.cmake` shims (one
  `CPMAddPackage` each), `epm.cmake` (the consumer module) and
  `epm-export.cmake` (injected into child builds).
- `Tests/` — NanoTest, one `<Area>Tests.cpp` per area.
- `Examples/` — consumer projects that exercise `epm.cmake`.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

`cmake --install build --prefix <dir>` installs only `bin/epm` and
`share/epm/cmake/epm.cmake`. Consumers need neither: `epm.cmake` downloads the
pinned release binary into `<cache>/tool/<ref>/`, or builds it there through
those same install rules (`Docs/PoC.md` §8).

Two versions: `project(epm VERSION ...)` is the source version
(`Apps/epm/Version.h.in` carries it into `epm --version`, release.yml names the
assets after it and checks it against a `v*` tag). `EPM_VERSION` in
`CMake/epm.cmake` is the binary version consumers get, with the five
`EPM_RELEASE_SHA256_<platform>` hashes of exactly that release. To release:
bump `project(VERSION)`, push to main, tag `vX.Y.Z`, wait for release.yml, then
set `EPM_VERSION` and paste the hashes from the release's `SHA256SUMS` (or
copy them from its `epm.cmake` asset).

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
