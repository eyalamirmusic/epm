# CLAUDE.md

## Git rules

Never commit, push, tag, or create releases unless the user explicitly asks for
it in the current request. This applies to subagents too: every agent brief must
say "do not commit or push". Leave changes in the working tree (or on the
current branch, uncommitted) and report them; the user decides when they land.

epm is a C++ package manager for CMake projects, built on the eacp framework
(`eacp-core`, and `eacp-network` for `IPC::Lock`). `Docs/Graph.md` is the
contract for the manifest (`epm.cpp`, a recipe the binary compiles into an
executable and runs in two phases: `describe()` declares the dependencies, and
the `build()` of each package's steps, `add<T>` or the default `CMakePackage`,
builds it into the cache), the dependency provider and `epm configure`;
`Docs/PoC.md` for the cache layout, fingerprint, child build and bootstrap, and
for the per-call `epm_add_package` flow kept for migration. `PLAN.md` is the
longer-term design and background.

## Layout

- `Lib/epm/` — namespace `epm::`, included as `<epm/Area/File.h>`, two static
  libraries. `epm-recipe` is what a recipe executable links, so it can run a
  build step on its own: `Spec/`, `Cache/` (but `CacheListing`),
  `Build/{ChildBuild,Harvest,Configs}` with the embedded scripts, `Util/` and
  the recipe API; it links eacp-core, eacp-network and Miro, and its headers
  and archives are the installed recipe SDK. `epm-core` is the driver, linking
  `epm-recipe`: `Graph/`, `Resolve/`, `Emit/`, `CacheListing` and
  `Recipe/{RecipeBuild,RecipeRun,RecipeSdk}`.
  - `Util/` — `Sha256`, `Git` (git CLI through `eacp::Processes`, never
    interactive), `Command` (child processes, `Echo` for streaming their
    output, `failureReason`, `lastLines`, `findExecutable`), `Log`
    (`--verbose`, `progress` lines), `Text`, `Timestamp`, `Paths`
    (`resolvedPath`: absolute, then `weakly_canonical`).
  - `Spec/` — `Env` (`env.json`, including `profile`), `Request` (and
    `decide`, the mode decision), `Manifest` (`ManifestPackage`, one dependency
    entry, `customBuild` for `add<T>`), `Overrides` (`overrides.json`, filled
    from the environment): Miro-reflected structs whose field names are the
    JSON keys; `JsonFile.h` loads and saves them. `Platform` names the target
    platform from `Env`.
  - `Recipe/` — `epm.cpp` recipes, each a standalone executable whose
    contract with epm is JSON. Recipe API (`epm-recipe`): `Recipe.h` (the
    header a recipe includes: `Recipe : CMakePackage`, `describe()`,
    `add`/`add<T>`, `PackageDecl`, `EPM_RECIPE`, which writes `main()`),
    `Package` (one package's build step: `fetch`, `fingerprint`, `cached`,
    `cmake`, `exportPrefix`/`useSource`, `runPackage`), `CMakePackage` (the
    default steps with `configure`/`afterBuild` hooks, `buildInProcess`),
    `CMakeBuild` (one child build per config: `option`/`define`/`configs`/
    `generator`/`into`, `configure`/`build`/`harvest`/`run`), `Prefix` (the
    entry `cached` hands a step), `BuildProtocol` (`BuildInput`/`BuildOutput`,
    the step's JSON), `BuildError` (what the helpers throw), `RecipeMain` (that
    `main()`: `[describe] --env --out`, `build --package --in --out [--quiet]`),
    `Project` (what `describe` fills, `projectError`). Driver (`epm-core`):
    `RecipeSdk` (the compiler, flags, headers and per-config archives: the
    install prefix's, found beside the binary, else the build tree's baked in
    through `RecipeSdk.cpp.in` and `file(GENERATE)`), `RecipeBuild` (one
    compiler call into `<cache>/recipe/<key12>/epm-recipe`), `RecipeRun` (runs
    it in a scratch directory and reads the `Project` back).
  - `Cache/` — `CacheRoot`, `SourceCache`, `BinaryCache` (entries under
    `bin/<name>/<platform>/<fp12>`, picking, additive config builds),
    `Fingerprint` (the ABI fingerprint and the `EPM_ABI_MATCH` policies),
    `CacheListing` (`epm cache ls`), `PackageLock` (eacp `IPC::Lock`, hence
    the `eacp-network` dependency).
  - `Build/` — `ChildBuild`, `Harvest`, `Configs` (config mapping and the
    facade's `$<CONFIG>` selector). `CMake/epm-export.cmake` and
    `CMake/epm-provider.cmake` (as the core of `epm-nested.cmake`, the
    provider of nested child builds) are compiled into `epm-recipe`, so into
    epm and every recipe executable, through `Build/EmbeddedScripts.cpp.in`.
  - `Emit/` — `Response` (the `responses/<name>.cmake` writer, and
    `cmakeQuoted`/`cmakeQuotedList`), `GraphResponse` (`graph.cmake` and
    `graph.json`), `RecipeSdkCMake` (`recipe-sdk.cmake`, the `RecipeSdk` as
    `EPM_RECIPE_*` variables).
  - `Graph/` — `Merge` (one package's effective request and its provenance),
    `ManifestLoader` (a directory's `epm.cpp` as a `Project`, and running a
    build step in a recipe executable: the interface the walk is given, and
    `RecipeManifestLoader`, the real one, which keeps each step's
    `<name>.in.json`/`.out.json`/`.command` in `<out>/steps/`), `Graph` (the
    manifest walk, notes, step owners, `dependencyClosure`, `resolvePlan`
    running the steps in dependency order: `epm configure`), `ResolvedGraph`
    (`graph.json`).
  - `Resolve/` — the `epm resolve` pipeline (default steps, in-process) and
    `buildInputFor`.
- `Apps/epm/Main.cpp` — the `epm` executable (`configure`, `graph-cmake`,
  `describe`, `recipe-sdk`, `resolve`, `cache`): `Apps::runConsole` plus
  manual subcommand dispatch on `argv[1]`, `Args::Parser` per subcommand.
- `CMake/` — vendored `CPM.cmake`, `Find<dep>.cmake` shims (one
  `CPMAddPackage` each), `get_epm.cmake` (the file consumers vendor: finds
  the binary, then registers the provider when there is an `epm.cpp`),
  `epm.cmake` (the consumer module, installed with the binary; also
  `epm_add_recipe`, which builds the project's `epm.cpp` as an executable
  target against `epm::recipe`, from `epm recipe-sdk`, and adds a test and a
  `<target>-describe` target running `<target> --env <build>/epm/env.json`;
  launching it with those arguments is how `describe` is debugged, with
  `build --package <n> --in <build>/epm/steps/<n>.in.json --out <file>` a
  build step the root owns (`steps/<n>.command` holds the exact command the
  configure ran, with epm's cached executable, for any owner, less the
  `--quiet` epm passes so a failure goes only to `--out`: run by hand, it
  prints the failure on stderr too), and
  `epm describe --exe` runs it in place of epm's own build),
  `epm-provider.cmake` (the dependency provider and `epm_load_package`,
  installed beside `epm.cmake`) and `epm-export.cmake` (injected into child
  builds).
- `Tests/` — NanoTest, one `<Area>Tests.cpp` per area (`RecipeTests` compiles
  and runs `Tests/Recipe/epm.cpp` with the real toolchain, both `describe` and
  `build --package`; `PackageTests` drives `Package`/`CMakeBuild` steps
  in-process against a scratch cache; `GraphTests` walks graphs, step owners
  included, over an in-memory `ManifestLoader`), plus `cmake -P`
  self-tests: `GetEpmRanges.cmake` (get_epm's `EPM_VERSION` range handling,
  ctest `get_epm_ranges`), `GetEpmSourceDir.cmake` (get_epm's
  `EPM_SOURCE_DIR` decisions, ctest `get_epm_source_dir`), `GetEpmLatest.cmake`
  (get_epm's reading of a stale or corrupt `tool/latest.json`, ctest
  `get_epm_latest`), `EpmConfigsAuto.cmake` (epm.cmake's
  `EPM_CONFIGS` resolution, ctest `epm_configs_auto`), `EpmOverrides.cmake`
  (the `overrides.json` writer, ctest `epm_overrides`), `GraphProvider.cmake`
  (the provider over fixtures in `Tests/Graph/`, ctest `epm_graph_provider`)
  and `GraphNested.cmake` (ctest `epm_graph_nested`: the real binary over two
  local git repositories from `Tests/Nested/`, Mid and the Leaf it declares
  with `add<LeafSteps>`, prebuilt and from source; Leaf's step runs in Mid's
  recipe executable, its option lands in Leaf's fingerprint and, in source
  mode, in `graph.json` and `graph.cmake`'s `OPTIONS`, the
  `<build>/epm/steps/` files are kept and Leaf's step is re-run by hand from
  them; plus the consumer's `epm_add_recipe` executable, its test and
  `describe --exe`; skipped without git and on Windows) and
  `InstallRoundTrip.cmake` (ctest `epm_install_round_trip`, not on Windows:
  the installed epm through a symlink, `recipe-sdk` and `describe` against the
  prefix only, and which `bin/epm` a second configuration's install leaves).
- `Examples/` — consumer projects: `HelloEacp` (`epm_add_package`) and
  `HelloGraph` (the same through `epm.cpp` and `find_package`).

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

A top-level single-config build defaults `CMAKE_BUILD_TYPE` to Release.
`cmake --install build --config <C> --prefix <dir>` installs only `bin/epm`
(the Release one; another config only when no Release one is there),
`lib/<C>/lib{epm-recipe,eacp-network,eacp-core,fmt,miniz,Miro,ResEmbed}.a`
(same names in every config, no LTO), `include/` (`epm`, `eacp/Core`,
`eacp/Network`, `Miro`, `ResEmbed`, `ea_data_structures`),
`share/epm/cmake/{epm,epm-provider}.cmake` and
`share/epm/epm-config` (one line: the configuration of the installed
`bin/epm`; Release always wins `bin/epm`); the
binary finds them relative to itself (`Docs/PoC.md` §8). On Linux,
eacp-network needs libcurl's development package (`find_package(CURL)`), and
recipes link libcurl by name (`-lcurl`, `-L<dir>` only for a non-default
directory: `epm_shared_library_options`). Consumers vendor
`CMake/get_epm.cmake`, which never downloads a binary (recipes link epm's
archives, so they must come from the local compiler). In order:
`EPM_EXECUTABLE`; `EPM_SOURCE_DIR` (or the enclosing checkout) built into
`<cache>/tool/local-<hash>/`; an in-range epm on PATH; the newest in-range
`<cache>/tool/v*/`; else the newest in-range tag from `git ls-remote`
(`EPM_REPOSITORY`, cached a day in `tool/latest.json`), shallow-fetched into
`<entry>/src`, built Debug then Release as a host build (consumer toolchain
unset) and installed into `<cache>/tool/<tag>/`, whose build dirs are then
pruned. Then it includes that entry's `epm.cmake`.
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
push the tag; release.yml publishes the archives (each an installed epm:
`bin`, `lib`, `include`, `share`), `SHA256SUMS` and `get_epm.cmake`; get_epm
itself downloads none of it and builds the tag from source. Never move or
delete a published tag.

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
