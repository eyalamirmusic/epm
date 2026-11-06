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

- `Lib/epm/` — namespace `epm::`, included as `<epm/Area/File.h>`, one static
  library, `epm-recipe`, linking eacp-core, eacp-network and Miro. It is both
  the recipe SDK, what a recipe executable links so it can run a build step on
  its own (`Spec/`, `Cache/`, `Build/{ChildBuild,Harvest,Configs}` with the
  embedded scripts, `Util/` and the recipe API, whose headers and the archives
  are the installed SDK), and the driver that epm links (`Graph/`,
  `Resolve/`, `Emit/`, `Cache/{CacheListing,CacheVerify}` and
  `Recipe/{RecipeBuild,RecipeRun,RecipeSdk}`, headers not installed).
  - `Util/` — `Sha256`, `Git` (git CLI through `eacp::Processes`, never
    interactive), `Command` (child processes, `Echo` for streaming their
    output, `failureReason`, `lastLines`, `findExecutable`), `Log`
    (`--verbose`, `progress` lines), `Text`, `Timestamp`, `Paths`
    (`resolvedPath`: absolute, then `weakly_canonical`; `renameNoReplace`, the
    publish rename, retried briefly on Windows' transient access-denied and
    sharing violations), `ProcessInfo` (this process's id, whether a pid is
    alive, the host name: what staging liveness reads; upstream candidates).
  - `Spec/` — `Env` (`env.json`, including `profile` and `epmExecutable`),
    `Request` (and
    `decide`, the mode decision), `Manifest` (`ManifestPackage`, one dependency
    entry, `customBuild` for `add<T>`), `Overrides` (`overrides.json`, filled
    from the environment): Miro-reflected structs whose field names are the
    JSON keys; `JsonFile.h` loads and saves them. `Platform` names the target
    platform from `Env`, says when a child build must be told the target
    system (`crossCompilesWithoutToolchain`), and tells the MSVC ABI
    (`msvcAbi`) and its runtime library (`msvcRuntimeOf`: the consumer's, else
    the static default, `staticMsvcRuntime`).
  - `Recipe/` — `epm.cpp` recipes, each a standalone executable whose
    contract with epm is JSON. Recipe API (`epm-recipe`): `Recipe.h` (the
    header a recipe includes: `Recipe : CMakePackage`, `describe()`,
    `add`/`add<T>`, `PackageDecl`, and `EPM_RECIPE`, a shorthand for the
    `main()` returning `recipeMain<T>(argc, argv)` that an `epm.cpp` writes),
    `Package` (one package's build step: `fetch`, `fingerprint`, `cached`,
    `cmake`, `exportPrefix`/`useSource`, `runPackage`), `CMakePackage` (the
    default steps with `configure`/`afterBuild` hooks, `buildInProcess`),
    `CMakeBuild` (one child build per config: `option`/`define`/`configs`/
    `generator`/`into`, `configure`/`build`/`harvest`/`run`), `Prefix` (the
    entry `cached` hands a step), `BuildProtocol` (`BuildInput`/`BuildOutput`,
    the step's JSON), `BuildError` (what the helpers throw), `RecipeMain` (that
    `main()`: `[configure] --manifest --env --overrides --out [--cache]
    [--verbose]`, the default, which is epm configure itself, run in the
    manifest's directory whatever directory it starts in;
    `describe --env --out`; `build --package --in --out [--quiet]`),
    `Project` (what `describe` fills, `projectError`). Driver:
    `RecipeConfigure` (`describedRecipe`/`runRecipeStep`, describe and a step
    in this process; `ConfigureArgs`, `configureInputs` and `runConfigure`,
    the configure shared by epm and the recipe executable; `SelfRecipeLoader`,
    the recipe executable's `ManifestLoader`: its own recipe described and its
    own steps run in-process, still keeping their `steps/` files with
    `<name>.command` naming this executable, every other recipe through
    `RecipeManifestLoader`), `RecipeSdk` (the compiler, flags, headers and
    per-config archives: the install prefix's, found beside the binary, or
    beside `env.epmExecutable` through `recipeSdkFor`, else the build tree's
    baked in through `RecipeSdk.cpp.in` and `file(GENERATE)`), `RecipeBuild`
    (one compiler call into `<cache>/recipe/<key12>/epm-recipe`, GCC and Clang
    spellings or cl's, for cl and clang-cl, which need the MSVC environment of a
    developer prompt), `RecipeRun`
    (runs its `describe` in a scratch directory and reads the `Project`
    back).
  - `Cache/` — `CacheRoot`, `SourceCache`, `BinaryCache` (entries under
    `bin/<name>/<platform>/<fp12>`, each config an immutable unit
    `configs/<Config>/` built in `.staging` and published by one rename with
    its small marker (`epm-config.json`, all a pick reads) and its hashed file
    list (`epm-files.json`, read only to verify or copy),
    `createEntry`/`describeStagedConfig`/`publishConfig`, picking (a complete
    entry before an exact one), `configUsableBy`, staging names carrying
    pid, host and time, and `removeStaleStaging`, which deletes only what a
    dead process of this host, or a week-old heartbeat of another host,
    left), `Fingerprint` (the ABI fingerprint, the
    `EPM_ABI_MATCH` policies, and `abiKey`, a fingerprint projected onto what
    a policy compares, which dependents record), `CacheListing`
    (`epm cache ls`), `CacheVerify` (`epm cache verify`), `CacheStore`
    (read-only cache stores, `EPM_CACHE_STORES`/`--cache-store`: `copyConfig`
    copies a published unit, checked against its marker and file list (paths,
    `gen/shared/` only for shared ones, the export and `.names`, the LTO kind
    of the bytes read, an LTO config's compiler), through staging and
    one no-replace rename; `pullFromStores`, what `Package::cached` takes from
    the stores when the local pick lacks configs; `epm cache push`/`pull`),
    `PackageLock` (eacp `IPC::Lock`, hence the `eacp-network` dependency; it
    only de-duplicates local work, publishing is atomic without it).
    `SourceCache` resolves a ref without fetching (`resolveSource`), fetches
    the snapshot (`fetchSource`, also under a lock the caller holds;
    `fetchCommit` for a commit resolved already, with the ref it came from
    for servers that refuse a fetch by hash), and fetches a package's
    `epm.cpp` alone for the graph walk (`fetchManifest`,
    `src/<name>/.recipes/`, copied from the snapshot when that is cached: a
    dependency's recipe is always compiled from that one file): a package found
    in the cache or a store is used without building it, its snapshot fetched
    afterwards only for the consumer's `SOURCE_DIR` (`EPM_FETCH_SOURCES`,
    `EPM_<Name>_FETCH_SOURCE`).
  - `Build/` — `ChildBuild`, `Harvest` (also the fixed facade,
    `targetsFileText`, which loads only the configs `EPM_FACADE_CONFIGS` names
    when it is set), `Lto` (`ltoKindOf`: LLVM bitcode, GCC LTO (ELF, COFF,
    Mach-O) or MSVC `/GL` objects in an archive, object or universal file,
    bounds-checked and never nesting deeper than slice, member), `Configs`
    (config mapping, Debug,
    Release and RelWithDebInfo each a config of its own and MinSizeRel built
    as Release, the facade's `$<CONFIG>` selector, and `configLinkProblem`,
    the MSVC ABI's rule that a Debug build links only a Debug config, checked
    at configure time by the graph walk and `epm resolve`). `CMake/epm-export.cmake` and
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
    `<name>.in.json`/`.out.json`/`.command` in `<out>/steps/`, written by
    `writeStepFiles`), `Graph` (the
    manifest walk, notes, step owners, `dependencyClosure`, `resolvePlan`
    running the steps in dependency order: `epm configure`), `ResolvedGraph`
    (`graph.json`).
  - `Resolve/` — the `epm resolve` pipeline (default steps, in-process) and
    `buildInputFor`.
- `Apps/epm/Main.cpp` — the `epm` executable (`configure`, `graph-cmake`,
  `describe`, `recipe-sdk`, `resolve`, `cache` with `path`, `ls`, `verify`,
  `push` and `pull`): `Apps::runConsole` plus
  manual subcommand dispatch on `argv[1]`, `Args::Parser` per subcommand.
  `configure` is a bootstrapper: it compiles the root `epm.cpp` into the cache
  and runs that executable's `configure` with the same (absolute) arguments
  in the manifest's directory, its stdout streamed, its stderr passed on and
  its exit code returned, an env naming this epm when `env.json` names none.
- `CMake/` — vendored `CPM.cmake`, `Find<dep>.cmake` shims (one
  `CPMAddPackage` each), `get_epm.cmake` (the file consumers vendor: finds
  the binary, then registers the provider when there is an `epm.cpp`),
  `epm.cmake` (the consumer module, installed with the binary; also
  `epm_add_recipe`, which builds the project's `epm.cpp` as an executable
  target against `epm::recipe`, from `epm recipe-sdk`, whose default
  arguments, and the `<target>-configure` target, run the whole configure in
  it (`configure --manifest <src> --env <build>/epm/env.json --overrides
  <build>/epm/overrides.json --out <build>/epm`, plus `--cache`/`--verbose`
  as `epm_run_binary` adds them; the inputs written by a deferred
  `epm_write_recipe_inputs`); launching it with those arguments is how
  `describe` and the root's hooks are debugged, with
  `build --package <n> --in <build>/epm/steps/<n>.in.json --out <file>` one
  build step the root owns (`steps/<n>.command` holds the exact command the
  configure ran, with the executable that ran it: epm's cached one for any
  owner under the CMake configure, this target after `<target>-configure`,
  less the `--quiet` epm passes so a failure goes only to `--out`: run by
  hand, it prints the failure on stderr too), and `epm describe --exe` runs
  its `describe` in place of epm's own build),
  `epm-provider.cmake` (the dependency provider and `epm_load_package`,
  installed beside `epm.cmake`) and `epm-export.cmake` (injected into child
  builds).
- `Tests/` — NanoTest, one `<Area>Tests.cpp` per area (`RecipeTests` compiles
  and runs `Tests/Recipe/epm.cpp` with the real toolchain, `describe`,
  `build --package` and a whole `configure` from a copy of the executable,
  Tiny's `add<Tiny>` step in-process, fetched from a local git repository
  named by `EPM_TEST_TINY_GIT`; `CacheStoreTests` copies, pushes and pulls
  units between scratch caches and stores; `PackageTests` drives `Package`/`CMakeBuild` steps
  in-process against a scratch cache; `BinaryCacheTests` also races two
  publishers of one config in threads, verifies corrupted entries and detects
  LTO objects, synthetic and from the real `cc -flto`; `GraphTests` walks
  graphs, step owners included, over an in-memory `ManifestLoader`), plus
  `cmake -P`
  self-tests: `GetEpmRanges.cmake` (get_epm's `EPM_VERSION` range handling,
  ctest `get_epm_ranges`), `GetEpmSourceDir.cmake` (get_epm's
  `EPM_SOURCE_DIR` decisions, ctest `get_epm_source_dir`), `GetEpmLatest.cmake`
  (get_epm's reading of a stale or corrupt `tool/latest.json`, ctest
  `get_epm_latest`), `EpmConfigsAuto.cmake` (epm.cmake's
  `EPM_CONFIGS` resolution, ctest `epm_configs_auto`), `EpmOverrides.cmake`
  (the `overrides.json` writer, ctest `epm_overrides`), `EpmEnv.cmake`
  (`env.json`'s Apple architecture and deployment-target fallbacks, its SDK
  directory and version, its C compiler and its generator platform, ctest
  `epm_env`), `PrefixMapVersions.cmake` (epm-export.cmake's
  `-ffile-prefix-map` only for a compiler version that takes it, ctest
  `epm_prefix_map_versions`), `GraphProvider.cmake`
  (the provider over fixtures in `Tests/Graph/`, ctest `epm_graph_provider`)
  and `GraphNested.cmake` (ctest `epm_graph_nested`: the real binary over two
  local git repositories from `Tests/Nested/`, Mid and the Leaf it declares
  with `add<LeafSteps>`, prebuilt and from source; Leaf's step runs in Mid's
  recipe executable, its option lands in Leaf's fingerprint and, in source
  mode, in `graph.json` and `graph.cmake`'s `OPTIONS`, the
  `<build>/epm/steps/` files are kept and Leaf's step is re-run by hand from
  them, and Mid's `${Leaf_SOURCE_DIR}/include` (which Mid's CMakeLists.txt
  requires) is copied into its entry; prebuilt packages' `SOURCE_DIR`s by
  default and with `EPM_FETCH_SOURCES=OFF`/`EPM_Mid_FETCH_SOURCE=ON`, and Mid
  from source over a prebuilt Leaf, whose snapshot it gets; the repositories
  serve partial fetches (`uploadpack.allowFilter`), as GitHub does;
  plus the consumer's `epm_add_recipe` executable: its `-configure`
  target, a whole configure in that executable, refreshing
  `<build>/epm/graph.cmake` with everything cached and the dependencies'
  steps still in their cached recipe executables, its `describe` command and
  `describe --exe`; skipped without git), `GraphIos.cmake`
  (ctest `epm_graph_ios`: the same fixtures for an iOS simulator consumer,
  `-DCMAKE_SYSTEM_NAME=iOS` without a toolchain file, under Xcode and Ninja:
  the child builds get the target system, the entries are
  `ios-simulator-<arch>` simulator archives and the app links; skipped off
  macOS and without the simulator SDK), `Relocate.cmake` (ctest
  `epm_relocate`: the real binary over two local git repositories from
  `Tests/Relocate/`, Reloc and the RelocDep it takes through CPM; Reloc's
  entry, built into one cache root, names nothing of it in its CMake files or,
  with GCC and Clang, its archive, carries its headers in its Release unit's
  `include/`, and serves a fresh consumer without a rebuild once the root is
  renamed, where `epm cache verify` passes, then fails on a corrupted archive;
  skipped without git), `CacheStore.cmake` (ctest `epm_cache_store`: the same
  fixtures; Reloc built into one cache, `epm cache push`ed into a store (a
  second push copies nothing), the store made read-only, and fresh consumers
  with empty caches and `EPM_CACHE_STORES` (CMake list, then environment)
  configure, build and run without building Reloc or, with
  `EPM_FETCH_SOURCES=OFF`, fetching its source, a corrupted copy of the store
  listed first being rejected with a note, and without it with Reloc's
  snapshot fetched for its `SOURCE_DIR`; skipped without git) and
  `InstallRoundTrip.cmake` (ctest `epm_install_round_trip`: the installed epm
  through a symlink (the binary itself on Windows), `recipe-sdk`, `describe` and a
  `configure` of an empty recipe against the prefix only, and which `bin/epm`
  a second configuration's install leaves).
- `Examples/` — consumer projects: `HelloEacp` (`epm_add_package`),
  `HelloGraph` (the same through `epm.cpp` and `find_package`) and
  `NestedGraph` (`Lib`, taking eacp prebuilt, and `App`, using the sibling
  `Lib` through `EPM_Lib_SOURCE`). Every example takes its dependencies
  prebuilt and, through `releaseIfPossible()` (`EPM_CONFIGS` under `MSVC` in
  HelloEacp), Release only where a Debug build links Release archives: on the
  MSVC ABI they follow the build type instead.

## Build

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

A top-level single-config build defaults `CMAKE_BUILD_TYPE` to Release. On
the MSVC ABI (cl, clang-cl) epm builds with the static runtime library
(`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>`, with
`CMAKE_POLICY_DEFAULT_CMP0091=NEW` for its dependencies), `epm.cmake` defaults
a consumer to the same, and child builds get the consumer's value, which is in
the fingerprint (`Docs/Graph.md` §2).
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
unset; Ninja, else Makefiles, whatever the consumer's generator: on Windows
without a ninja, or a Visual Studio consumer outside a developer prompt, the
consumer's) and installed into `<cache>/tool/<tag>/`, whose build dirs are then
pruned. Then it includes that entry's `epm.cmake`.
`CMake/epm.cmake` requires `EPM_EXECUTABLE` and has no bootstrap of its own.
A `get_epm.cmake` inside an epm checkout (`<dir>/CMake/`, `<dir>/CMakeLists.txt`
with `project(epm`) defaults `EPM_SOURCE_DIR` to that checkout, so the examples,
which list `../../CMake/get_epm.cmake`, build epm from this checkout (an
incremental build into `<cache>/tool/local-<hash>/`); `-DEPM_SOURCE_DIR` or
`-DEPM_EXECUTABLE` overrides it. Examples hold only what a real consumer writes.
`EPM_CACHE_STORES` (CMake list or environment variable) names read-only cache
stores whose binaries a configure copies into the cache before building
(`Docs/PoC.md` §3); they reach every build step through its input JSON, and
never a child build, which sees only the local cache.

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
