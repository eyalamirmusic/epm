# epm graphs — the manifest, the provider and the resolved graph

This document is the contract for the second stage of epm: dependency graphs declared
in a C++ manifest (`epm.cpp`, a recipe the binary compiles and runs: what a project
depends on, and how its packages are built), consumed through CMake's dependency
provider, resolved once per configure by the binary. It supersedes the per-call
`epm_add_package` flow of `Docs/PoC.md` §1–§3; the cache layout, fingerprint, config
mapping and child build of `Docs/PoC.md` stay as they are except where this document
says otherwise.

Principles:

- CMake keeps only `find_package` and `target_link_libraries`. Every declaration,
  option, config and policy lives in `epm.cpp`; every decision is taken in the binary
  or in the recipes it runs.
- Source mode is the definition of a package; a binary entry is a cache of building
  that definition. A package's identity is what source mode receives: git url,
  resolved commit, configs, and the options its build step settles (the request's,
  plus whatever the step sets on its CMake build, §2). Source mode and a binary build
  get the same options, and the fingerprint records them.
- A dependency's targets are spelled the same in source and binary mode.

## 1. User code

```cmake
cmake_minimum_required(VERSION 3.25)
list(APPEND CMAKE_PROJECT_TOP_LEVEL_INCLUDES CMake/get_epm.cmake)
project(App CXX)

find_package(fmt REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE fmt::fmt)
```

`epm.cpp` next to the top-level `CMakeLists.txt`:

```cpp
#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
// How fmt is built into the cache: the default CMake flow, plus a hook.
struct Fmt final : CMakePackage
{
    void configure(CMakeBuild& build) override { build.option("FMT_DOC", "OFF"); }
};

struct App final : Recipe
{
    void describe() override
    {
        add<Fmt>("fmt", "fmtlib/fmt", "11.0.2").option("FMT_TEST", "OFF");
        auto b = add("B", "org/B", "v2.1.0").config("Release");

        if (env().profile == "ci")
            b.mode("binary-only");
    }
};
} // namespace

int main(int argc, char** argv)
{
    return recipeMain<App>(argc, argv);
}
```

`describe()` says what the project depends on; `Fmt` says how one of those packages is
built when epm builds it into the cache (§2). A package declared with plain `add` gets
the default steps, which build any CMake project.

Source fallback and local checkouts never touch CMake code:

```bash
cmake -B build -DEPM_MODE=source            # every package from source
cmake -B build -DEPM_fmt_MODE=source        # one package from source
cmake -B build -DEPM_B_SOURCE=~/Code/B      # a local checkout, added as a subdirectory
cmake -B build -DEPM_PROFILE=ci             # env().profile for the recipes
```

The same names work as environment variables (§3 has the limits).

A library can pin a dependency to source, and the app that uses it can take the
binary cache back for its Release builds (the root wins, §5; the library's declaration
becomes a note):

```cpp
// Lib's epm.cpp
void describe() override { add("Leaf", "org/Leaf", "v1").mode("source"); }

// App's epm.cpp: the root only adds to what Lib declares, no git, no tag.
void describe() override
{
    add("Lib", "org/Lib", "v1");

    if (env().isMultiConfig || env().buildType == "Release")
        add("Leaf").mode("prebuilt");
}
```

Mode is per package, not per config, so on a multi-config generator the root caches
every config. `Examples/NestedGraph` has the same shape, a library and an app using
it from a sibling directory, with eacp prebuilt and Release only in both.

## 2. Manifest `epm.cpp`

The manifest is a recipe: a C++ file named `epm.cpp` beside the top-level
`CMakeLists.txt` of the consumer, and of every package that has dependencies, or build
steps, of its own. epm compiles it into a small executable, the *recipe executable*,
and runs it; everything that passes between processes is JSON, so no C++ ABI crosses
a process boundary. The root's recipe executable runs the configure itself (`epm
configure` compiles it and runs its `configure` command, §6) and compiles and runs
every other recipe in the graph the same way. The project may also build it itself,
for its IDE, diagnostics and debugger (`epm_add_recipe`, below).

A recipe is two phases of user code, each run in its recipe executable:

1. **Describe.** `describe()` declares the packages the project depends on, for the
   consumer's `env()`. The configure runs it for every recipe in the graph while it
   walks it (§5): the root's in its own process, every other as `<recipe> describe`,
   whose `epm::Project` (`Recipe/Project.h`) it reads back.
2. **Build steps.** A `Package`'s `build()` turns one package's source into what the
   consumer links: a binary cache entry, or the source directory. The configure runs
   it once for every package it resolves, in dependency order, in the executable of
   the recipe that owns the package's step (§5 rule 9): a step the root owns in its
   own process, every other as `<recipe> build --package <name>` (§6). A package in
   source mode runs its step too: nothing is built, but its hooks run, and the step
   records the source directory and the options the consumer's `add_subdirectory`
   gets.

### Describe: `Recipe`

- `#include <epm/Recipe/Recipe.h>`, then `using namespace epm;`, derive from
  `Recipe` and override `describe()`. The file writes the executable's `main()`,
  which returns `epm::recipeMain<Type>(argc, argv)` (`Recipe/RecipeMain.h`; it hands
  the factory overload `recipeMain(argc, argv, create)` a `RecipeFactory` making a
  `Type`, as a `std::unique_ptr<Recipe>`). User code owns `main()`: `recipeMain<T>`
  is only the default command-line parser (`configure`, `describe`, `build`, and the
  usage with exit code 2 on an unknown command), so a recipe that wants commands of
  its own checks `argv[1]` before delegating to it. `EPM_RECIPE(Type)`, once per file
  at global scope, is a shorthand for that `main()`. The directive brings in every
  name the recipe headers declare in `epm` (`Recipe`, `PackageDecl`, `Package`,
  `CMakePackage`, `CMakeBuild`, `Prefix`, `BuildError`, `Project`, `Env`, `Request`,
  `ManifestPackage`, the cache and fingerprint types, …); a recipe's own type or
  function of one of those names is ambiguous with it.
- `Recipe` derives from `CMakePackage`, which derives from `Package` (below): a recipe
  is also the build steps of its own project, which run when that project is another
  project's dependency (§5 rule 9). They are the default steps unless the recipe
  overrides `build()`, or the `configure`/`afterBuild` hooks, as any package may.
- `add(name, git, tag)` appends a dependency built by the default steps.
  `add<T>(name, git, tag)` appends one built by `T`'s steps: `T` derives from `Package`
  (checked at compile time), usually through `CMakePackage`. It sets the entry's
  `customBuild`, and `build --package <name>` constructs a fresh `T` to run. `git` and
  `tag` may be left out (below).
- Both return a `PackageDecl`, whose `option(key, value)`, `config(config)`,
  `mode(mode)` and `abiMatch(abiMatch)` each set one field and return the handle, so
  calls chain; `package()` is the `epm::ManifestPackage` (`Spec/Manifest.h`) itself.
  The handle stays valid across later `add` calls. Unset fields are empty; the binary
  applies defaults. (`Project::add` and the free functions `setOption`, `addConfig`,
  `setMode` and `setAbiMatch`, which these wrap, remain for code that holds a
  `Project`.) `releaseIfPossible()` is `config("Release")` wherever a Debug build of
  the project links a Release build of the package, which is everywhere but the MSVC
  ABI (cl, clang-cl: `env().cxxCompilerFrontend`, `msvcAbi`), whose debug and release
  runtimes do not mix; there it leaves the configs as they are, `auto` unless the recipe
  set some, so a Debug build caches a Debug build of the package (`PoC.md` §9, which
  also says what the binary refuses). The examples use it for eacp.
- `env()` is the consumer's `env.json` (`Spec/Env.h`): generator, build type,
  compiler (`cxxCompilerId`, `cxxCompilerFrontend`), `systemName`, `systemProcessor`,
  `crossCompiling`, `osxArchitectures` (for an Apple device or simulator SDK, the ones
  it builds by default when the consumer sets none), deployment target, Android's ABI,
  API level, NDK and STL (`androidAbi`, `androidApi`, `androidNdk`, `androidStl`), the
  MSVC runtime library (`msvcRuntime`) and the rest, plus `profile`; `PoC.md` §2.
  A consumer cross-compiling without a toolchain file (`-DCMAKE_SYSTEM_NAME=iOS`,
  `-DCMAKE_SYSTEM_NAME=Android`) has its child builds told the target system and
  sysroot, or Android's settings, too; one using the NDK's toolchain file has them
  given its `ANDROID_*` variables (`PoC.md` §5). Recipes are still compiled for and
  run on the host. Every recipe in the graph, the root's and each dependency's, is described
  for the same `env`, in both phases. A recipe's `describe` run without `--env`
  (below), and so `epm describe` without one, uses an empty one.
- `project()` is what `describe()` declared.

| Field         | Values                                                            |
|---------------|-------------------------------------------------------------------|
| `name`        | the `find_package` name                                           |
| `git`         | `owner/repo`, `https://host/path.git`, `git@host:path.git`, a path |
| `tag`         | a branch, tag or commit                                           |
| `options`     | `KEY` → `VALUE`, set as plain variables in source mode            |
| `configs`     | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`, or `auto`     |
| `mode`        | `auto`, `prebuilt`, `binary-only`, `source`                       |
| `abiMatch`    | `strict`, `compatible`, `any`                                     |
| `customBuild` | set by `add<T>`: this recipe has build steps for the package      |

- The order of the `add` calls is kept: it is the declaration order of §5 rule 3.
- `name` is case-sensitive (as the PoC). A later stage may relax this. A package
  without a name is an error, and so is a name added twice to one project
  (`projectError`); the error names the package whose `epm.cpp` it is.
- `git` without `://`, `git@` or a local path's start (a leading `/`, or a drive letter
  such as `C:/` on Windows) is a GitHub `owner/repo` shorthand.
- `configs` names are the consumer-facing names; the binary collapses them as
  `epm.cmake` does today (§3 of `PoC.md`).
- The root project may add a package with only `options` (or `configs`, `mode`,
  `abiMatch`), as `add("fmt").option(...)`: `git` and `tag` then come from the first
  dependency that declares it, and the configure fails if none does.
- A recipe may use anything the static library `epm-recipe` (the recipe API, the
  specs, the source and binary caches, the child build and harvest, `Util`),
  `eacp-core` (`eacp::FilePath`, `eacp::Strings`, `eacp::Processes`, …),
  `eacp-network` and Miro offer: the executable links them statically. The same
  library holds the driver (the graph walk, `epm resolve`, compiling and running
  recipes, the response writers), whose headers are not part of the installed recipe
  API; the executable's `main()` runs it for its `configure` command (below).
- Stores and lock files are not part of this stage.

Profiles are code. `EPM_PROFILE` (a CMake variable, else the environment) reaches the
recipes only as `env().profile`, and a recipe branches on it like on any other `env`
field. epm gives the name no meaning of its own and keeps it out of every fingerprint;
what a recipe does with it (an option, a config, a mode) is what counts.

### Build steps: `Package`

A `Package` (`Recipe/Package.h`) is one package's build step. epm gives it its context
(a `BuildInput`, below) and runs `build()`, which ends by recording the result:
`exportPrefix(prefix)` (the package is prebuilt, from that entry, with the options of
the fingerprint the step asked for, `prefix.wanted()`), or `useSource(build)` (the
consumer adds it from source, with that `CMakeBuild`'s `definedValues()`) or plain
`useSource()` (from source, with the request's options). A `build()` that records
neither fails the step, and so does one that records prebuilt for a package in source
mode (`spec().mode`): epm fails the configure with `the build step of <name> recorded
prebuilt, but the package is in source mode: …`. The helpers are members and take no
context arguments:

| Member                                | What it is                                                           |
|---------------------------------------|----------------------------------------------------------------------|
| `spec()`                              | the effective, merged request (§4): name, git url and ref, mode, options, configs (concrete), abiMatch, buildMissingBinaries, deps |
| `env()`, `cache()`, `verbose()`       | the consumer's env, the cache root, `--verbose`                      |
| `dependencies()`                      | the direct dependencies, resolved before this step: name, mode and, when prebuilt, the entry's fingerprint hash, prefix, `epm-targets.cmake` and configs |
| `dependencyGraph()`                   | `graph.cmake` of the dependency closure (§10); empty without dependencies |
| `resolveSource()`                     | the source's commit and `sourceId`, all `fingerprint()` needs, without fetching it (as epm resolved it before the step; else `git ls-remote`). Its `directory` is set only when the checkout is there already (a snapshot in the cache, an `EPM_<Name>_SOURCE` override) |
| `fetch()`                             | the source checkout: the one epm handed over, else the commit `resolveSource()` names fetched into the source cache (cheap once a snapshot is complete). Inside `cached()`'s callback it fetches under the package lock `cached()` holds. Called outside the callback, it also makes the checkout the package's `SOURCE_DIR` when the step ends with `exportPrefix()` (§7) |
| `fingerprint(source)`                 | the ABI fingerprint (`PoC.md` §3) of `spec()` for that source        |
| `fingerprint(source, build)`          | the same with `build`'s options and definitions in place of `spec().options` |
| `cached(wanted, buildMissing)`        | the binary entry, as the mode decides (below)                        |
| `cmake(source)`, `cmake(source, prefix)` | a `CMakeBuild` of the source, from `spec().options` and the prefix's configs (below) |
| `exportPrefix(prefix)`, `useSource(build)`, `useSource()` | record the result (above)                        |
| `log(line)`                           | a progress line, `-- epm: <name>: <line>`                            |

`cached(wanted, buildMissing)` is the cache logic of `PoC.md` §5 and §9:

- In mode `source` it returns nothing (the consumer builds from source).
- It picks an entry for `wanted` under `spec().abiMatch` (default `compatible`) and
  the request's configs (`pickEntry`, ranked as `PoC.md` §9 says); when that pick
  lacks a needed config, it copies what the cache stores hold into the local cache
  first (`PoC.md` §3, *Cache stores*) and picks again. It logs the pick, and lets
  `decide` (`Spec/Request.h`) choose: an entry
  holding every config this compiler can link (`configUsableBy`: a config of LTO
  objects only links with the compiler that built it, `PoC.md` §9) is used as it is;
  `auto` with `buildMissingBinaries` off and no complete entry returns nothing;
  `binary-only` throws, naming what the cache lacks.
- Otherwise, under the package lock (`Cache/PackageLock`; a timeout is an error), it
  picks again (another process may have built the entry meanwhile) or creates the
  entry (whole, by one rename), and calls `buildMissing` once with a `Prefix` whose
  `configs()` are those to build and whose `stagedEntry()` is a fresh private
  directory laid out as the entry (`bin/<name>/<platform>/.staging/<name>`), which the
  build goes into. Once the callback has returned, each config it left harvested in
  its staged unit (an export file with its `.names`) is published into the entry: its
  marker, with the hash of every file, then one rename of the unit (`PoC.md` §5); one
  it left without fails the step. A config of LTO objects goes into the exact entry for
  `wanted` instead when the entry picked was recorded for another compiler. Nothing is
  published earlier: a callback that throws (a failed child build, an `afterBuild`
  hook that throws) publishes none of them and its staging is deleted, so a first
  build that fails leaves an entry holding only `manifest.json` and the facade, which
  the next run picks and builds into. The lock only spares a second local build:
  publishing is atomic, and of two processes that build the same config the second
  finds the first's unit and drops its own.
- It returns a `Prefix` (`Recipe/Prefix.h`): `path()` (`bin/<name>/<platform>/<fp12>`),
  `configDirectory(config)` (inside the callback, the config's staged unit, where a
  hook adds files; afterwards the published one, never modified),
  `entry()`, `wanted()` (the fingerprint asked for, which under `compatible` or `any`
  may differ from the entry's; the difference is the configure note) and `configs()`
  (inside the callback, the configs to build; afterwards, those that call built, empty
  when the entry was complete).

Every helper throws `BuildError` (`Recipe/BuildError.h`: a one-line `what()`, and
`details` such as the tail of a child build's output) when it fails, and a `build()`
may throw it too; the step reports it as its error. Any other exception becomes
`the build step of <name> threw: <what>`.

The default steps, `Package::build()` (and `CMakePackage::build()` with its hooks):

```cpp
auto source = resolveSource();                          // commit and sourceId, no fetch
auto build = cmake(source);
configure(build);                                       // hook
auto wanted = fingerprint(source, build);

if (auto prefix = cached(wanted, [&](const Prefix& missing) {
        build.source(fetch().directory)                 // only when it builds
            .into(missing)
            .run();
        afterBuild(missing);                            // hook
    }))
    exportPrefix(*prefix);
else
    useSource(build);                                   // source mode: fetches; the hook's options too
```

So the default steps fetch a package's source only to build it or for source mode: a
package whose entry is in the cache, or in a cache store, is used without its source
(the configure fetches it afterwards for the consumer's `SOURCE_DIR` unless
`EPM_FETCH_SOURCES` is off, §7).
A hook that reads the source calls `fetch()` itself (which also keeps the package's
`SOURCE_DIR`, §7). Steps of their own that call `fetch()` before `cached()`, as recipes
written before this did, keep working unchanged and always fetch.

`CMakePackage` (`Recipe/CMakePackage.h`) adds the two hooks, empty by default:

- `configure(CMakeBuild&)` is called once, before the fingerprint, whether or not the
  entry needs building, and in source mode too: what it sets is part of the entry's
  fingerprint, so changing it makes a new entry, and is what a source package's
  `add_subdirectory` gets.
- `afterBuild(const Prefix&)` is called after the missing configs are built into the
  prefix, before they are checked into the entry: to add files to it. When it throws,
  none of them is added, and the next run builds them again.

A hook's options and definitions therefore reach both modes. In source mode the step
records them (`useSource(build)`, the output's `options`), `graph.json` and
`graph.cmake`'s `OPTIONS` carry them (§6, §7), and the provider sets them as plain
variables before `add_subdirectory` (§8). A step of its own that ends with plain
`useSource()` records none, and the request's options stand.

### `CMakeBuild`

`CMakeBuild` (`Recipe/CMakeBuild.h`) is a CMake build of a package's source into a
binary cache prefix, one child build per config, with the mechanics of `PoC.md` §5.
It starts from the request's options and the prefix's configs; each setter returns the
build:

- `option(key, value)`: a package option, `-D<key>=<value>`, over the request's value.
- `define(name, value)`: a raw `-D<name>=<value>`, after epm's own definitions and the
  options, so it may override one of them: `define("CMAKE_CXX_STANDARD", "17")`,
  `define("CMAKE_POSITION_INDEPENDENT_CODE:BOOL", "ON")`.
- `configs(list)`: the configs `run()` builds; by default the prefix's.
- `generator(name, makeProgram)`: overrides the generator epm chose (the consumer's
  when it is single-config, else Ninja, else Unix Makefiles).
- `into(prefix)`: the prefix to build into, for a build made before `cached` chose one.

The steps, each throwing `BuildError` and needing a prefix: `configure(config)` (a
fresh build directory `<cache>/build/<name>/<platform>/<fp12>/<config>`, the injected
scripts, `cmake -S -B`), `build(config)` (`cmake --build`), `harvest(config)`
(relocates the build's output into the prefix, `harvestConfig`: its export file and
`.names`; it does not add the config to the entry, which `cached` does once its
callback has returned, so a step that fails after a harvest leaves nothing that passes
for built), and `run()` (the three for each config, with a progress line before and
after each build, then deletes the build trees unless a harvest had to keep one).
`definedValues()`, the options and definitions with a definition, keyed by its name
without a `:TYPE` suffix, winning over an option of the same name (so
`define("X:BOOL", "ON")` and `option("X", "ON")` are one key), is what
`fingerprint(source, build)` records and `useSource(build)` hands a source build;
`buildDirectory`, `childBuild` and `configureArguments` show what a config's child
build would run.

### The recipe executable

`Recipe/RecipeMain` gives it three command lines:

```
<recipe> [configure] [--manifest <epm.cpp>] --env <env.json>
         --overrides <overrides.json> --out <dir> [--cache <dir>]
         [--cache-store <dir>...] [--verbose]
<recipe> describe [--env <env.json>] [--out <project.json>]
<recipe> build --package <name> --in <input.json> --out <output.json> [--quiet]
```

`configure` is the default: a first argument starting with `-` is one of its options,
while any other word that is not `configure`, `describe` or `build`, or no argument at
all, prints the usage and exits with 2. It is `epm configure` (§6), run in the recipe's own
executable (`Recipe/RecipeConfigure`: `runConfigure` over a `SelfRecipeLoader`), with
the same options; `--manifest` is the recipe's own file, `epm.cpp` in the working
directory by default. Before anything else it changes its working directory to the manifest's
directory. It reads the env and overrides, describes the recipe in its own process
(the `--manifest` file, compared by resolved path, is itself), and walks the graph as
§5 says: every other recipe is fetched, compiled into the cache against the recipe SDK
of the env's `epmExecutable` (below) and described as a process of its own. Then it
runs the build steps in dependency order, those the root owns (§5 rule 9) in its own
process and every other in its owner's executable, and writes `graph.json`,
`graph.cmake` and `steps/` into `--out` (§6). A step it runs itself keeps its
`steps/` files as one run in another executable does, `<name>.command` naming this
executable, so it can be run again by hand. A failure goes to stderr as `epm:
<error>`, its details above it, with exit code 1: `epm configure` passes that stderr
on as its own.

`describe` reads the `Env` (an empty one without `--env`), runs
`describe()` once and writes the `Project` as JSON (Miro reflection of `Project`) to
`--out`, or to stdout without it. An unreadable env, an exception out of `describe` or
a project that `projectError` rejects goes to stderr as one `epm-recipe: …` line, with
exit code 1 and nothing written.

`build` reads the `BuildInput`, which must have schema 1 and be for the package
`--package` names, takes `--verbose` from it, runs `describe()` with the input as
context (so `env()` is the consumer's), then the `build()` of the package's steps: the
`Recipe` itself when the package is the input's `recipeName`, else the steps `add<T>`
registered for it, else a plain `CMakePackage`; but when the input's `customBuild` is
set and `describe()` registered no `add<T>` for the package, the step fails (`epm
expects custom build steps for <name> (add<T>) from this recipe, but its describe()
registers none for it`) rather than run the default steps in their place. It writes the
`BuildOutput` to `--out` whether or not the step succeeded, an exception out of
`describe()` included (`the recipe threw: …`), and exits with code 1 when it failed.
The failure goes to stderr as well (its details, then `epm-recipe: <error>`) unless
`--quiet` is given: the configure passes it to every step it runs in another
executable and prints the failure itself, once (§6); the command it keeps for a run by
hand omits it. An input it cannot
read, of another schema (`the build input has schema 2, but this recipe reads schema
1`) or for another package is reported on stderr only, with no output written.
Progress lines, and child build output under `--verbose`, go to stdout as the step
runs.

The two files (`Recipe/BuildProtocol.h`, Miro-reflected, field names as the JSON keys):

- `BuildInput`: `schema` (1); `recipeName` (the package whose `epm.cpp` this is, as
  the graph names it; empty for the root's); `env`; `cacheRoot` (resolved); `request`
  (the effective request, its `deps` holding each direct dependency's fingerprint
  hash, and `compatibleDeps` and `anyDeps` the same entries' keys under those
  policies, §10); `cacheStores` (the cache stores, in order, `PoC.md` §3); `source`
  (optional: the `commit` and `sourceId` epm resolved, and the `directory` of the
  checkout when there is one already, else empty and `fetch()` fetches that commit; an
  `EPM_<Name>_SOURCE` checkout has a directory but no commit or sourceId; without a
  source at all `fetch()` resolves the request's ref); `dependencies` (one per direct
  dependency: `name`, `mode`, and when prebuilt `fingerprint`, `prefix`, `targetsFile`,
  `configs`); `dependencyGraph`; `dependencySources` (`name`, `gitUrl`, `commit` of
  each package of the closure whose `SOURCE_DIR` `dependencyGraph` leaves empty, §10);
  `epmExecutable` (the epm binary of the configure: the env's `epmExecutable`, else
  the running executable; a child build with dependencies gets it as
  `EPM_EXECUTABLE`); `verbose`; `customBuild` (the owning declaration came
  with steps of its own, `add<T>`: the recipe must have them).
- `BuildOutput`: `schema`; `name`; `mode` (`prebuilt` or `source`, empty when the step
  failed); `sourceDirectory`; `options` (what the package is built or configured with:
  the `definedValues()` of the `CMakeBuild` that `useSource(build)` recorded, or the
  options of the fingerprint `exportPrefix` recorded, a hook's included; empty after a
  plain `useSource()`, when the request's stand); when prebuilt `prefix`, `platform`,
  `configs`, `note` (how the entry's toolchain differs from the current one) and
  `fingerprintHash` (full hex, which dependents record in their `deps`) with
  `compatibleKey` and `anyKey` (the entry's `abiKey` under those policies, which they
  record in `compatibleDeps` and `anyDeps`); when the step failed `error` (one line)
  and `details`.

Both stay schema 1. Each side reads the other's schema on its own first and reports a
mismatch as such rather than half reading the file: the recipe as above, epm as
`<command> wrote schema 2 output, but this epm reads schema 1`.

### How the binary builds and runs a recipe

`Recipe/RecipeBuild`, `Recipe/RecipeRun`, `Graph/ManifestLoader`:

1. `buildRecipe` compiles and links it with one direct compiler call, no CMake. The
   call is the recipe SDK (`Recipe/RecipeSdk`) of an epm binary, the env's
   `epmExecutable` or else the running one (in epm itself, the same binary): the
   compiler that built epm,
   its C++ standard and dialect, the flags of the configuration epm was built in
   followed by `-O1` (a recipe is a few calls; a quick compile beats `-O3`), sysroot,
   architectures and deployment target, `epm-recipe`'s include directories,
   definitions and compile options; after the source, the static archives of epm's
   configuration (`epm-recipe`, `eacp-network`, `eacp-core`, `Miro` and the static
   libraries under them: `fmt`, `miniz`, `ResEmbed`), each before those it depends on,
   the order a single-pass linker needs; then the link options (frameworks, system
   libraries; libcurl on Linux, which `eacp-network` links). Under cl or clang-cl
   (`RecipeSdk::compilerFrontend`, `clCommandLine`) the same in cl's spelling: `/std:`,
   the configuration's flags as they are (their `/O` level; a Debug `/RTC1` takes no
   `/O` after it), the runtime library of epm's archives (`/MT`, `/MTd`, the static one
   epm builds with by default: `RecipeSdk::msvcRuntime`, which a recipe's objects must
   match), `/I`, `/D`, the
   object beside the executable (`/Fo`), `/Fe:`, then the archives and the link options
   after `/link`, a system library as `<name>.lib`, with CMake's standard libraries
   (`kernel32.lib`, `gdi32.lib`, ...; `CMAKE_CXX_STANDARD_LIBRARIES`, which every link
   of epm's build got) among the link options. cl finds its headers and libraries
   through `INCLUDE` and `LIB`, so epm must run in the MSVC environment of a developer
   command prompt, which `buildRecipe` checks before compiling. An imported shared (or
   unknown-type) library is named, not given by the build machine's path to it:
   `-l<name>` for `lib<name>.so`, `-l:<file>` for a versioned `lib<name>.so.<N>`,
   after `-L<dir>` only when the compiler does not search that directory already
   (`epm_shared_library_options` in `Lib/epm/CMakeLists.txt`); frameworks and static
   archives keep their paths. GCC and Clang spellings only.
2. The SDK is located once per process and epm binary (`recipeSdkFor`), so a recipe
   executable running the configure finds epm's from the env's `epmExecutable`. An
   installed epm (`<prefix>/bin/epm`, symlinks resolved) uses `<prefix>/include` and
   every `<prefix>/lib/<Config>/` that holds all the archives, and needs its own
   configuration among them. Otherwise (a build tree, an incomplete install) it uses
   the build tree's paths, baked into `epm-recipe` at build time
   (`Recipe/RecipeSdk.cpp.in`, through `file(GENERATE)`, once per configuration).
3. Executables are cached in `<cache>/recipe/<key12>/epm-recipe` with `compile.log`
   beside it. The key hashes the SDK's identity (all of the above, with only epm's own
   configuration's archives), the contents of those archives and of epm's headers, the
   epm binary itself and the recipe's source, so an unchanged recipe is compiled
   once per epm binary and its libraries, and an edited library or header, or a
   rebuilt epm, gets fresh executables. The compile runs under the package lock
   `recipe-<key12>`, into a temporary name moved into place on success.
4. `runRecipe` runs it as `epm-recipe describe --env <env.json> --out <project.json>`,
   both files in a scratch directory of their own per run (several processes may run one
   recipe at once, for different envs), with the `epm.cpp`'s directory as its working
   directory, so a file it reads by a relative path is its own package's, and reads the
   `Project` back.
5. A recipe that does not compile, exits non-zero, is killed (it crashed) or writes no
   project fails the configure, which epm reports like any other error: the recipe ran
   in a process of its own. The error names the package. A compile failure adds the
   log and the compiler's first diagnostic:
   `the manifest of B (<src>/epm.cpp): the recipe <src>/epm.cpp did not compile
   (/usr/bin/c++ exited with code 1); see <cache>/recipe/<key12>/compile.log`, then
   the first `error:` line on its own line. A run failure adds how it ended
   (`<cache>/recipe/<key12>/epm-recipe exited with code 1`, `… was terminated by
   signal 6 (exit code 134)`, `… exited without writing its project`), then the last
   20 lines it printed. A file without a `main()` fails to link.
   The root's recipe is described in the configure's own process instead: what it
   throws is `the manifest <epm.cpp>: the recipe threw: …`, and a crash ends the
   configure, which `epm configure` reports (§6).
6. A build step runs in the same cached executable, compiled first if need be:
   `RecipeManifestLoader::build` runs `build --package <name>` in the `epm.cpp`'s
   directory, with its input, output and command line kept in `<out>/steps/` (§6).
   A step the root owns runs in the configure's own process (`SelfRecipeLoader`),
   keeping the same files. The compile lock `recipe-<key12>` is held only while
   compiling, never while a recipe runs.

`epm describe [<epm.cpp>] [--exe <path>] [--env <env.json>] [--cache <dir>] [--verbose]`
prints the project a recipe describes, as JSON, for checking one by hand: it builds
the file as `configure` does and runs its `describe` (`runRecipe`). `--exe` runs that
recipe executable's `describe` (`epm_add_recipe`'s, below, or any program whose `main()`
returns `recipeMain<T>`) instead of compiling the file, so the positional then only names the working
directory (an `--exe` alone runs in the current one) and may be left out. `configure`
has no such option: it always builds and runs its own.

### The recipe as a target of the project: `epm_add_recipe`

```cmake
epm_add_recipe(AppRecipe)            # [SOURCE <file>]
```

`epm_add_recipe` (`epm.cmake`) adds `epm.cpp` (or `SOURCE`, relative to the current
source directory) to the project's own build as the executable `AppRecipe`: the same
program `buildRecipe` makes, all three commands of it, so the IDE indexes it with
epm's headers, a compile error shows in the normal build, and a debugger can run it.
It is linked to `epm::recipe` and built at epm's C++ standard and extensions, without
IPO; on Apple, when `CMAKE_OSX_ARCHITECTURES` is set, for the host processor only (a
recipe is a host program, and epm's archives need not hold the other architectures).
The configure never runs this executable: `epm configure` compiles and runs its own
copy of the same source in the cache (§6), as this one exists only once the project
is built. Its run arguments are `epm configure`'s own, for the whole configure in this
executable:

```
configure --manifest <source> --env <build>/epm/env.json
          --overrides <build>/epm/overrides.json --out <build>/epm
```

plus `--cache <dir>` when `EPM_CACHE` is set and `--verbose` under `EPM_VERBOSE`, as
epm's run gets them. The custom target `AppRecipe-configure` (folder `epm`, not built
by default) builds it and runs that in the top-level source directory: it describes,
fetches, finds or builds the cache entries and rewrites `<build>/epm/graph.json`,
`graph.cmake` and `steps/`, running the steps the root owns in its own process. The
executable itself is part of `all`, so every build compiles `epm.cpp` with the
project's toolchain. That `env.json` and `overrides.json` are the configure's own,
written by a deferred call at the end of the configure even when nothing asked epm for
a package.

**Debugging `describe` and the root's steps.** Build the Debug configuration, so the
executable has debug info, launch `AppRecipe` under the debugger with its run
arguments and stop in `describe` or in a hook (or `build()`) of a step the root owns.
Xcode and Visual Studio already have those arguments, and the top-level source
directory as working directory, in the target's scheme or debugger settings; in CLion,
give the `AppRecipe` run configuration those arguments and that working directory
(the `AppRecipe-configure` target's command shows them); by hand, `lldb --
<build>/AppRecipe configure --manifest epm.cpp --env <build>/epm/env.json --overrides
<build>/epm/overrides.json --out <build>/epm`. `AppRecipe describe --env
<build>/epm/env.json` prints the project alone, and `epm describe epm.cpp --exe
<build>/AppRecipe --env <build>/epm/env.json` reads it the way the configure does. The
executable the CMake configure runs is epm's own cached one: the same file and
libraries, but compiled with epm's configuration flags and `-O1` (under cl, their own
`/O` level).

**Debugging a build step.** The configure keeps each step's input in
`<build>/epm/steps/<name>.in.json` (§6). Launch `AppRecipe` with
`build --package <name> --in <build>/epm/steps/<name>.in.json --out <any file>`, from
the top-level source directory, and stop in the steps' `build()` or hooks: the input is
complete (request, env, cache, source, dependencies), so the step does what the
configure's did, and an entry already in the cache is found rather than rebuilt (delete
the entry to step through a build). A failure goes to the `--out` file and, as `--quiet`
is not given, to stderr too. `<name>.command` holds the command that runs the step,
with the executable that ran it (for a step the root owns, epm's cached copy of the
root's recipe after a CMake configure, `AppRecipe` after `AppRecipe-configure`), and
where it ran, less the `--quiet` the configure passes to another executable, so
running it by hand prints a failure as well. `AppRecipe` runs only the steps of
packages whose step the root owns (`recipeName` empty in the input, §5 rule 9); given
another recipe's input it would run its own idea of the package's steps, so for those
use the command file, whose executable is that recipe's.

`epm::recipe` is an `INTERFACE IMPORTED GLOBAL` target defined once per configure from
`epm recipe-sdk --out <build>/epm/recipe-sdk.cmake`, which writes the binary's SDK
(`Recipe/RecipeSdk.h`, `Emit/RecipeSdkCMake`) as CMake variables:
`EPM_RECIPE_CXX_COMPILER`, `EPM_RECIPE_COMPILER_ID`, `EPM_RECIPE_COMPILER_FRONTEND`
(`CMAKE_CXX_COMPILER_FRONTEND_VARIANT`), `EPM_RECIPE_CXX_STANDARD`,
`EPM_RECIPE_CXX_EXTENSIONS`, the lists `EPM_RECIPE_INCLUDE_DIRS`,
`EPM_RECIPE_COMPILE_DEFINITIONS` and `EPM_RECIPE_COMPILE_OPTIONS`,
`EPM_RECIPE_CONFIGS` (every configuration the SDK has archives for), one
`EPM_RECIPE_LINK_LIBRARIES_<CONFIG>` per configuration (upper-case suffix; the same
archives, in the same order, in each), on the MSVC ABI one
`EPM_RECIPE_MSVC_RUNTIME_<CONFIG>` per configuration (the runtime library its archives
were compiled against) and `EPM_RECIPE_LINK_OPTIONS`. The target carries the include
directories (as system directories, like any imported target's), the definitions,
`cxx_std_<N>`, `epm-recipe`'s compile options only under a compiler of epm's family,
and links, in order, one `STATIC IMPORTED` target per archive (`epm::recipe-<name>`),
then the link options. Each archive target has a location per configuration and a
`MAP_IMPORTED_CONFIG_<CONFIG>` for the standard configurations and the project's own:
the same-named configuration when epm has it, else Debug for Debug and Release for
anything else (RelWithDebInfo, MinSizeRel, a custom or empty configuration), then the
other of the two. On the MSVC ABI the recipe target's `MSVC_RUNTIME_LIBRARY` follows
the same mapping (a Debug build of the project against a Release-only build tree epm
compiles the recipe with `/MT`, the static runtime epm builds with by default): objects
and archives compiled against different runtimes do not link (LNK2038). A project whose
`CMAKE_MSVC_RUNTIME_LIBRARY` is of the other kind than epm's (DLL against static, §2)
gets one warning: the recipe executable takes epm's runtime, its dependencies the
project's. So it works the same with an installed epm as with one built from a
checkout: the SDK always comes from the binary.

- The archives hold epm's C++ types, compiled by epm's compiler, and another compiler
  family need not share their ABI. A project whose compiler family (its
  `CMAKE_CXX_COMPILER_ID`; cl and clang-cl are one family, as clang-cl's id is Clang
  with an MSVC `CMAKE_CXX_COMPILER_FRONTEND_VARIANT`) is not epm's stops the configure,
  with the ways out:
  `-DCMAKE_CXX_COMPILER=<epm's>`, an epm built with this compiler (delete its
  `get_epm.cmake` tool entry to rebuild it), or `-DEPM_RECIPE_ALLOW_COMPILER_MISMATCH=ON`,
  which makes it a warning. Another compiler of the same family (compared by real
  path) gets one status line; the target builds with the project's compiler.
- A `recipe-sdk.cmake` without `EPM_RECIPE_CONFIGS` (an older epm) or with
  inconsistent library lists stops the configure. When an SDK include directory or
  archive does not exist (an epm whose build or install tree has moved),
  `epm_add_recipe` does nothing beyond a status line.
- It does nothing when cross-compiling (a recipe runs on the host, against epm's host
  libraries), and in a project that is not the top-level one (a package added from
  source); a child build epm runs gets a no-op stub from `epm-export.cmake`, so a
  package may call it for its own developers.

Limits:

- The SDK must still be where it was: the compiler that built epm, and the install
  prefix (or build tree) holding the headers and archives. `get_epm.cmake` builds epm
  from source on the machine that uses it (`PoC.md` §8), so both are there; a bare
  `bin/epm` copied away from its prefix falls back to its build tree's paths.
- On Windows, the MSVC environment: cl and clang-cl take their headers and libraries
  from `INCLUDE` and `LIB`, which a developer command prompt (vcvarsall) sets and a
  Visual Studio generator's MSBuild sets for its own compiles only. A configure run
  outside one fails at the first recipe, saying so.
- On the MSVC ABI, one runtime library throughout. epm builds itself and its recipe
  libraries with the static runtime (`MultiThreaded$<$<CONFIG:Debug>:Debug>`, its
  top-level `CMakeLists.txt`), and `epm.cmake` defaults `CMAKE_MSVC_RUNTIME_LIBRARY` to
  the same for the project that includes it, in the including scope before any target
  (a top-level include runs inside `project()`, before the compiler is known, so it is
  set everywhere and read only by cl and clang-cl), with `CMAKE_POLICY_DEFAULT_CMP0091`
  for subdirectories whose `cmake_minimum_required` predates the variable. The
  project's value, or CMake's DLL default when it set the variable empty, goes into
  `env.json` as `msvcRuntime` and from there into every child build and the
  fingerprint (`PoC.md` §2, §5, §9), so the dependencies always match the project's
  objects. A project that chose the other kind (set the variable before the include, or
  with `-D`) keeps it; its recipe executable still links epm's archives, so
  `epm_add_recipe` compiles it with epm's runtime (§7) and warns once that the two
  differ. Static and dynamic entries never share a cache directory. A Debug build and a
  release build do not mix either: `releaseIfPossible()` above, and `PoC.md` §9 for what
  the configure refuses.

## 3. Overrides `overrides.json`

Miro-reflected `epm::Overrides` (`Spec/Overrides.h`), written by `epm_write_overrides`
(`epm.cmake`) under `${CMAKE_BINARY_DIR}/epm/` from the CMake variables it can see. The
binary fills what the file leaves unset from its own environment (`withEnvironment`);
CMake variables win over environment.

```json
{
  "schema": 1,
  "mode": "", "configs": [], "abiMatch": "", "strict": false,
  "buildMissingBinaries": true,
  "fetchSources": true,
  "packages": {
    "<name>": { "mode": "", "source": "/abs/path", "configs": [], "options": {},
                "fetchSource": false }
  }
}
```

CMake collects `EPM_MODE`, `EPM_CONFIGS`, `EPM_ABI_MATCH`, `EPM_STRICT`,
`EPM_BUILD_MISSING_BINARIES`, `EPM_FETCH_SOURCES` and every `EPM_<Name>_MODE`,
`EPM_<Name>_SOURCE`, `EPM_<Name>_CONFIGS`, `EPM_<Name>_OPTIONS` (a `;`-list of
`KEY=VALUE`, a bare `KEY` meaning `ON`) and `EPM_<Name>_FETCH_SOURCE` it finds among
normal and cache variables. Empty values are left out;
`EPM_<Name>_SOURCE` is made absolute against `CMAKE_SOURCE_DIR` (a leading `~` is the
home directory).

- `EPM_PROFILE` is not an override: `epm_write_env` puts it into `env.json` as
  `profile`, for the recipes (§2).
- `configs` stays empty unless the user set `EPM_CONFIGS`. `["auto"]` is an explicit
  request for the PoC's `auto` rule and beats the manifest.
- `buildMissingBinaries` is optional: written only when `EPM_BUILD_MISSING_BINARIES`
  is set; unset means on.
- `fetchSources` (`EPM_FETCH_SOURCES`) and a package's `fetchSource`
  (`EPM_<Name>_FETCH_SOURCE`, which wins for that package) are optional the same way,
  unset meaning on: whether a prebuilt package's snapshot is fetched for its
  `SOURCE_DIR` (§7).
- `EPM_STRICT` from the environment can only turn strict on, since a CMake `OFF`
  cannot be told from an unset one.
- `EPM_<Name>_*` environment variables are read only for names in the graph.

## 4. Merge order

Each package ends up as one effective `Request` (today's `Spec/Request.h`, extended by
`deps`). Values are taken from the first source that sets them, in this order:

1. Overrides, per package (`EPM_<Name>_*`), then global (`EPM_MODE`, …).
2. The root project's package entry.
3. The project of the dependency that declared the package (graph rules, §5).
4. Defaults: `mode=auto`, `configs=auto` (the PoC rule), `abiMatch=compatible`.

Options merge by key, with the same precedence per key. `source` is only an override
and makes the package `mode=source` with that directory instead of a checkout.
`buildMissingBinaries` is only an override (default on). A profile is no layer of its
own: it already shaped the projects through `env().profile` (§2).

The binary records, for every effective value, which source set it (`override`,
`override:package`, `root`, `manifest:<declarer>`, `default`, or
`source dependency <name>` for rule 6 of §5);
`epm why <name>` prints that (later; the data is kept in `graph.json` now).

## 5. Graph rules

1. The root recipe is run. Every declared package's ref is resolved to a commit and
   its `epm.cpp` alone is fetched (`PoC.md` §4: the snapshot when the cache has it,
   else a checkout of that one file), or taken from `source`. If the package has an
   `epm.cpp`, it is run too (§2) and its packages are added to the graph,
   recursively; one without has no dependencies. Discovery therefore happens before
   configure continues, by running recipes, not by executing CMake. The rest of the
   source is fetched by the package's build step, only when it builds the package or
   the package is in source mode (§2), and for the dependencies of a package built
   from source (§7, §10).
2. The root wins: a package declared by the root keeps the root's values whatever a
   dependency declares.
3. Among dependencies, the first declaration in a depth-first walk of the root's
   packages in declaration order (the order of each recipe's `add` calls) wins.
   Within one package, each declared dependency's subtree is walked before the
   package's own later declarations, as CPM's first add would see them.
4. Every later declaration that sets a field (git, tag, options, configs, mode,
   abiMatch) to something other than the winning value is reported as a note naming
   both declarers. A tag is bare, other fields labelled:
   `fmt: B declares 10.2.1, root declares 11.0.2`, `fmt: B declares mode source,
   root leaves it unset`, `fmt: B declares option FMT_TEST=ON, C declares option
   FMT_TEST=OFF`. `strict` turns the notes into an error and the configure fails.
5. Packages whose git urls name the same repository (`repositoryKey`, `Spec/Manifest`:
   scheme, user, trailing `/` and `.git` dropped, scp-like `host:path` as `host/path`,
   lower case; a local path or `file://` URL resolved, keeping its case) are reported
   in the same notes list, so `strict` fails on them too. The same key goes into the
   `sourceId` (`PoC.md` §3).
6. A package in `source` mode forces every package that depends on it into `source`
   mode (a binary cannot link a target that only exists in the consumer's build).
7. Packages are resolved in dependency order, so a package's dependencies have cache
   entries before its own child build runs. A package whose dependency ended up in
   source after all (an `auto` fallback) goes to source too.
8. `binary-only` fails the configure on a cache miss. `prebuilt` builds a missing
   entry. `auto` builds a missing entry unless `buildMissingBinaries` is off, in which
   case it falls back to source. `source` never consults the binary cache.
9. Every package's build step has one *owner*, the recipe whose executable runs it
   (§2, §6):
   - A package with an `epm.cpp` of its own (in its checkout, or in its
     `EPM_<Name>_SOURCE` directory) always owns its step: its `Recipe`'s `build()`
     runs, with `recipeName` its own name, whether or not it overrides anything. Custom
     steps (`add<T>`) that any project declares for such a package fail the
     configure: `<declarer> declares custom build steps for <name>, which has its own
     epm.cpp`.
   - Otherwise the owner is the recipe of the winning declaration (rules 2 and 3) when
     that declaration has custom steps; else of the first declaration that has, in
     the same order (the root's, then the dependencies' in walk order); else of the
     winning declaration, with the default steps. A declaration's recipe is the root's
     `epm.cpp` (`recipeName` empty) or the declaring dependency's (`recipeName` its
     name).
   - When several declarations have custom steps, only the owner's run; every other
     one is reported in the notes (`fmt: C declares custom build steps, which do not
     run: B's steps run`), so `strict` fails on it.
   - The owner decides only which code runs. The step's request is the merged one
     (§4), so the root's options, configs and mode reach it whoever owns it: a root
     that only tunes a package's options keeps a dependency's custom steps for it.

`source` is a real `add_subdirectory` of the package's cached snapshot into
`<build>/_deps/<name>-build`, its options set before it as normal variables (CMP0077
NEW, §8). It differs from one written by hand in four ways: the snapshot has no
`.git`; its tag is re-resolved with `git ls-remote` on every configure; it is not
`EXCLUDE_FROM_ALL`; and it forces the packages that depend on it into source (rule 6).
To build a working checkout instead, point the package at it with
`EPM_<Name>_SOURCE=<dir>`.

## 6. The binary: `epm configure`

```
epm configure --manifest <epm.cpp> --env <env.json> --overrides <overrides.json>
              --out <dir> [--cache <dir>] [--cache-store <dir>...] [--verbose]
```

epm compiles the root recipe (`buildRecipe` against the SDK of the env's
`epmExecutable`, cached, §2) and runs its `configure` command (§2, *The recipe
executable*) in the manifest's directory, every path absolute and the cache root
resolved:

```
<cache>/recipe/<key12>/epm-recipe configure --manifest <epm.cpp> --env <env.json>
    --overrides <overrides.json> --out <dir> --cache <cache>
    [--cache-store <store>...] [--verbose]
```

The cache stores are those given as `--cache-store`, else `EPM_CACHE_STORES` from
epm's environment, resolved here and passed explicitly (`PoC.md` §3).

Its stdout (progress lines, the steps' output) reaches epm's as it runs, its stderr
(the failure it reports, `epm: …`) is passed on as epm's own, and its exit code is
epm's. A recipe executable killed by a signal reported nothing itself: epm fails with
`the manifest <epm.cpp>: <executable> configure failed: <executable> was terminated by
signal 6 (exit code 134)`; on Windows `crashed (exit status 0xC0000409)`), then the
last 20 lines it printed. An env without
`epmExecutable` (epm run by hand) is copied into a temporary directory naming this
epm, and the copy is passed, so the recipe executable finds the SDK and nested child
builds get their `EPM_EXECUTABLE` (§10).

The configure runs the root recipe for the `env.json` given, then every dependency's,
planning the graph (§2, §5); then resolves the packages in dependency order, each
through its build step; and writes into `<dir>` (`<build>/epm/` under
`get_epm.cmake`):

- `graph.cmake` — the response the provider reads (§7).
- `graph.json` — the resolved graph: every package's effective request, source
  directory, commit, mode, the options its step recorded (`options`, else the
  request's), entry path, fingerprint, direct dependencies, and the provenance of each
  value. Nested child builds and `epm why` read it.
- `steps/` — each build step's files (below). It lives in the consumer's build
  directory, not in the cache.

### Build steps

For each package, in dependency order:

1. A package with a dependency that ended up in source goes to source (§5 rules 6 and
   7). Otherwise its request's `deps` get each direct dependency's `fingerprintHash`,
   and `compatibleDeps` and `anyDeps` its `compatibleKey` and `anyKey`, as that
   dependency's step reported them (§10), and the step gets `dependencies` and
   `dependencyGraph` (`graph.cmake` of the closure, §10).
2. The configure compiles the owner's recipe (§5 rule 9; cached, §2) and, in the
   `epm.cpp`'s directory, runs
   `<recipe executable> build --package <name> --in <dir>/steps/<name>.in.json
   --out <dir>/steps/<name>.out.json --quiet`. Beforehand it writes `<name>.in.json`
   and `<name>.command` (a `# run from: <directory>` line, then the command line, shell
   quoted (on Windows with double quotes, for cmd and PowerShell), without `--quiet`, so
   a run by hand prints a failure on stderr) and removes
   the previous `<name>.out.json`, so it cannot pass for this run's. The input carries
   the owner's `customBuild`. A step the root owns runs in the configure's own
   process instead (`SelfRecipeLoader`), with the same three files: `<name>.command`
   names the running executable and the root's directory, and the configure writes
   `<name>.out.json` itself.
   The three files are overwritten by the next configure and otherwise kept,
   so a step can be run again by hand, under a debugger (§2, `epm_add_recipe`).
3. The step's stdout reaches the configure's output as it runs: its progress lines
   always (`-- epm: building eacp from source into the cache (Release, macos-arm64),
   this can take a while`, `-- epm: built eacp (Release) in 21s`), its child builds'
   output under `--verbose` (`EPM_VERBOSE`). The result travels only through `--out`.
4. A step that failed fails the configure with its `error`, above it its `details`,
   printed once, by the configure: a step in another executable printed none of it
   (`--quiet`), and its progress was already streamed. A step that wrote no output,
   or exited non-zero while its output claims success, fails with the command, how it
   ended (exit code, or the signal that killed it) and the last 20 lines it printed.
5. A prebuilt result's `fingerprintHash` goes into its dependents' `deps` (and its
   keys into `compatibleDeps` and `anyDeps`); a source
   result sends them to source. A step asked for source (`spec().mode`) that recorded
   prebuilt fails the configure. The result's `options` go into `graph.json` and
   `graph.cmake` (§7).

A package in source mode runs its step as well, in the same executable: nothing is
built (`cached` returns nothing), but its `configure` hook runs and the step records
the options the provider sets before `add_subdirectory` (`useSource(build)`, §2, §8).
The default steps run in the configure's process without any recipe's `describe()`
(`buildInProcess`) only for a package whose owner has no recipe file (a root given
none, as tests do).

The configure holds no package lock while a step runs, in its own process or another:
the step takes the package's lock itself, inside `cached`, and only to build. eacp's
`IPC::Lock` is not re-entrant, so a lock held by the configure would make the step wait
out the timeout.

Measured with `Examples/HelloGraph` on macOS: a cold configure takes 66 s, of which
41 s build epm itself (Debug and Release, `PoC.md` §8) and 21 s build eacp (Release),
streamed live; a warm reconfigure takes under a second; running eacp's step again by
hand from `steps/` takes 0.01 s (the entry is found).

`epm resolve` (the per-package PoC command, for `epm_add_package`) always runs the
default steps in its own process: it has no recipe, so no custom steps. It stays until
`epm_add_package` is removed.

## 7. Response `graph.cmake`

```cmake
set(epm_PACKAGES "fmt;C;B")                     # dependency order
set(epm_fmt_MODE prebuilt)                      # prebuilt | source
set(epm_fmt_SOURCE_DIR "/cache/src/fmt/<sourceId12>") # empty: EPM_FETCH_SOURCES=OFF
set(epm_fmt_DEPS "")                            # direct dependencies, names
set(epm_fmt_OPTIONS "FMT_DOC=OFF;FMT_TEST=OFF") # the step's options, KEY=VALUE list
# prebuilt only:
set(epm_fmt_PREFIX "/cache/bin/fmt/<platform>/<fp12>")
set(epm_fmt_TARGETS_FILE "${epm_fmt_PREFIX}/epm-targets.cmake")
set(epm_fmt_PLATFORM "macos-arm64")
set(epm_fmt_CONFIGS "Release")                  # those this consumer can link
set(epm_fmt_NOTE "")                            # ABI note as today
set(epm_NOTES "fmt: B declares 10.2.1, root declares 11.0.2")
```

`OPTIONS` is what the package's step recorded (§6), hooks included (`FMT_DOC` comes from
§1's `Fmt::configure`), else the request's options.

`SOURCE_DIR` (and so the provider's `<name>_SOURCE_DIR`) is the snapshot, for a source
package and, by default, for a prebuilt one too: the top-level consumer gets the
snapshot of every package of its graph, direct or transitive, as `SOURCE_DIR`, so a
project reading CMake modules or headers from there (Catch2's
`list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)`) works whether the package
was built or found. The configure fetches those snapshots after every step, by the
commit the step resolved: no fingerprint and no step's input depend on it, and the
entry is the same either way.

`EPM_FETCH_SOURCES=OFF` (or `EPM_<Name>_FETCH_SOURCE=OFF` for one package; `ON` turns
one back on under a global `OFF`) skips that fetch, so a package found in the cache or a
cache store is used without cloning its source (`PoC.md` §4), the fast path for
developers and CI that only consume prebuilts. Its `SOURCE_DIR` is then empty, unless
something needs the source anyway:

- a package the consumer builds from source (mode `source`, or an
  `EPM_<Name>_SOURCE` override) depends on it, directly or not: those dependencies'
  snapshots are fetched whatever the setting, since such a package may read
  `${<dep>_SOURCE_DIR}` (`target_include_directories(Mid PUBLIC
  ${Leaf_SOURCE_DIR}/include)` for a header-only dependency);
- its step asked for it, calling `fetch()` outside `cached()`'s callback (a hook, or
  steps of its own that fetch first, §2).

A child build that loads the closure (§10) is not the consumer: its graph names the
snapshots of its prebuilt dependencies only as a dependent built from source needs
them, fetched by the step only when it builds (`dependencySources`).
`epm_add_package` follows the same settings (`epm resolve --no-source-dir`).

`CONFIGS` are the entry's configs this consumer can link (`configUsableBy`): a config of
LTO objects of another compiler, which a compatible entry picked for the others may
hold, is left out, and the provider sets `EPM_FACADE_CONFIGS` to `CONFIGS` around the
facade's include, so the facade (`epm-targets.cmake`, the same text in every entry)
loads only those and its `$<CONFIG>` selector maps every consumer config onto them.

## 8. CMake: `get_epm.cmake` as the top-level include

`get_epm.cmake` keeps its bootstrap (find or build the binary, `PoC.md` §8) and is now
listed in `CMAKE_PROJECT_TOP_LEVEL_INCLUDES`, so it runs inside the first `project()`.
It only drives functions of the installed module: `epm.cmake` (`epm_use_manifest`,
`epm_configure_graph`, `epm_write_overrides`, `epm_run_binary`) and
`CMake/epm-provider.cmake` beside it (`epm_provider_register`,
`epm_provide_dependency`, and `epm_load_package`, which `epm_add_package` shares).
`epm-provider.cmake` is installed next to `epm.cmake`.

1. If `EPM_NESTED` is set, return at once: the parent injected the provider (§10). A
   nested package therefore cannot use `epm_add_package`.
2. Bootstrap the binary and include its `epm.cmake`, as today.
3. If there is no `epm.cpp` next to the top-level `CMakeLists.txt`, stop; the
   project may still call `epm_add_package`.
4. Fail clearly if this file was `include()`d (after `project()`) rather than listed
   in `CMAKE_PROJECT_TOP_LEVEL_INCLUDES`, or if the found epm is too old to have
   `epm_use_manifest`.
5. `epm_use_manifest(<epm.cpp>)` registers the dependency provider
   (`cmake_language(SET_DEPENDENCY_PROVIDER … SUPPORTED_METHODS FIND_PACKAGE
   FETCHCONTENT_MAKEAVAILABLE_SERIAL)`; CMake allows that only at `project()` time)
   with `epm_configure_graph` as its one-time preparation.

`epm configure` runs lazily, at the provider's first call (the first `find_package`
or `FetchContent_MakeAvailable`), not at `project()`, so a `CMAKE_CXX_STANDARD` set
after `project()` still reaches `env.json`. That first call writes `env.json` (as
today, plus `profile` and `epmExecutable`, `PoC.md` §2) and `overrides.json` (§3),
runs `epm configure` (its stdout,
the build steps' progress included, reaching the configure output as it runs; a
non-zero exit fails the configure with its stderr), includes `graph.cmake` and prints
its notes once.
A fatal error inside the provider is followed by CMake's own "Could not find
XConfig.cmake" message.

The provider, on `find_package(X …)` or `FetchContent_MakeAvailable(X)`:

- If `X` is not in `epm_PACKAGES`, return without setting `X_FOUND`; CMake proceeds
  normally.
- Otherwise load `X` once, after first loading every name in `epm_X_DEPS` the same way:
  - `source`: set each `epm_X_OPTIONS` entry as a plain variable (CMP0077/CMP0126
    NEW, as today) and `add_subdirectory(${epm_X_SOURCE_DIR}
    ${CMAKE_BINARY_DIR}/_deps/X-build)`.
  - `prebuilt`: `include(${epm_X_TARGETS_FILE})` and make the imported targets
    GLOBAL, as today.
- `find_package` gets `X_FOUND TRUE`, `X_SOURCE_DIR`, `X_MODE`;
  `FetchContent_MakeAvailable` gets `FetchContent_SetPopulated(X)`. One status line
  is printed as today.
- Version arguments to `find_package` are ignored in this stage.

`EPM_CONFIGS` and `EPM_ABI_MATCH` are cache variables defaulting to empty, and
`EPM_BUILD_MISSING_BINARIES` is no longer an `option()`, so no CMake default overrides
a manifest; `epm_add_package` applies the old defaults (`auto`, `compatible`, `ON`)
itself. Build directories created before this keep the old cached values until
cleared.

`epm_add_package` remains in `epm.cmake` during migration and is removed once the
examples use manifests. `epm.cmake` is then reduced to the manifest flow, and
`get_epm.cmake` stays the only file consumers vendor.

## 9. Target names: aliases travel with the export

Source mode defines whatever the package's CMake defines, typically `fmt` plus the
alias `fmt::fmt`. The facade must define the same spellings.

- No CMake property lists ALIAS targets, so `epm-export.cmake` wraps `add_library` to
  record them as they are made. This collides with any dependency that also wraps
  `add_library` (the two wrappers recurse).
- Beside `export-<Config>.cmake` and its `.names` it writes `.aliases`, one
  `<alias> <target>` line per alias of an exported target. Aliases of imported
  targets are left out with them.
- The facade (`epm-targets.cmake`, the same file in every entry, `PoC.md` §5) reads
  every published config's `.aliases` when it is loaded and defines, after the facade
  targets, `add_library(<alias> ALIAS <target>)` for each line whose target it
  exported (`if (NOT TARGET <alias>)`).
- The `.aliases` is part of the config's unit, published and hashed with it.

## 10. Nested child builds

A package's child builds run inside its build step (`CMakeBuild`, §2), in the recipe
executable that owns it (the configure's own process for a step the root owns) or, for
`buildInProcess` (§6), in the configure's process.
`Build/ChildBuild` passes to the child configure of a package that has dependencies (a
non-empty `dependencyGraph` in the step's input), in addition to today's flags (a
package without dependencies gets today's flags only):

```
-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<builddir>/epm-export.cmake;<builddir>/epm-nested.cmake
-DEPM_NESTED=ON
-DEPM_GRAPH=<builddir>/epm-graph.cmake
-DEPM_EXECUTABLE=<epm>
```

`<epm>` is the epm binary of the configure (the input's `epmExecutable`, from the
env's: `EPM_EXECUTABLE`, as `epm_write_env` writes it, §6), never a recipe executable,
even when the graph runs in one. The step writes all three files into the child's
build directory before it configures. `epm-graph.cmake` is §7's `graph.cmake` for the package's
dependency closure only (`dependencyClosure` in `Graph/Graph.h`, computed by epm and
handed over as `dependencyGraph`), in dependency order, without notes.
Its prebuilt packages' `SOURCE_DIR`s are empty there (it is computed before the
consumer's fetch of §7), and the step's input lists them (`dependencySources`, with
the ref each commit was resolved from); the first child configure of the step fetches
each by the commit epm resolved (a server that refuses a fetch by hash is asked for
that ref instead) and appends `set(epm_<name>_SOURCE_DIR "<snapshot>")` to
`epm-graph.cmake` (`CMakeBuild::configure`), so a package building against them reads
`${<name>_SOURCE_DIR}` as in a source build, and a step that finds its entry fetches
nothing. The harvest copies what the export takes from those snapshots into the unit
(`include/dep/<name>/…`).
`epm-nested.cmake` is `CMake/epm-provider.cmake`, compiled into `epm-recipe`, and so
into epm and every recipe executable, like `epm-export.cmake`
(`Build/EmbeddedScripts.cpp.in`), followed by
`epm_provider_register("${EPM_GRAPH}")`: the same provider as §8, so there is one
provider implementation. Inside a child build every dependency is prebuilt (rules 6
and 7 of §5 guarantee it), so the provider only ever includes targets files. The
package's own `get_epm.cmake`, if it has one, returns immediately because
`EPM_NESTED` is set, without looking for a binary (so the package cannot use
`epm_add_package` there). `epm graph-cmake --graph <json> --out <graph.cmake>`
re-emits §7 from §6's JSON for tools; nested builds do not use it.

The fingerprint (`Cache/Fingerprint`) gains `deps` (from the request's `deps`): a
sorted map of direct dependency name → the fingerprint hash of the dependency's entry
actually used, which under `EPM_ABI_MATCH` may be a compatible entry rather than the
one requested. Changing a dependency therefore changes the entry of everything that
links it. The fingerprint `schema` stayed 2 then (entries got new `fp12` names anyway,
because the canonical JSON had `deps`); it is 4 now, without machine paths (`PoC.md`
§3).

Beside `deps` it records `compatibleDeps` and `anyDeps`: for each direct dependency,
the `abiKey` of the entry used under that policy, the hash of its fingerprint
projected onto what the policy compares (`abiProjection`: `compatible` drops the
full compiler and C compiler versions, the processor, the architectures, the system
name and the SDK's minor version; `any` keeps the schema, platform, runtime, options
and source; both put the dependency's own keys for the policy in its `deps`, so the
projection is recursive). Each policy compares the dependencies its own way
(`sameDependencies`): `strict` compares `deps`, every hash, as before; `compatible`
and `any` compare their keys (a dependency without one falls back to its `deps`
hash). So a Mid entry CI built against CI's Leaf entry matches, under `compatible`, a
machine whose own Leaf entry is compatible with CI's (another patch version of the
compiler): both Leaf entries have the same compatible key, while their `deps` hashes,
which `strict` still requires to be equal, differ. Without this, every machine that
picks its own exact Leaf would rebuild Mid. The keys are in the canonical form, so
they are part of `fp12`, and are the same on every machine for the same entries.

A package's `epm-targets.cmake` does not re-export its dependencies' targets: the
export skips imported targets already, and the provider loads `epm_X_DEPS` first.
`export()` accepts links to imported targets outside the export set and writes them
by the imported target's own name, so an exported library linking `Leaf::Leaf` (an
alias of Leaf's facade) carries `INTERFACE_LINK_LIBRARIES "Leaf"`, which the
consumer's provider has defined by then. `Tests/GraphNested.cmake` checks all of
this end to end, with Leaf's step run in Mid's recipe executable (Mid declares it with
`add<LeafSteps>`).

## 11. Out of scope for this stage

Remote cache stores other than directories (`PoC.md` §3 has directory stores), `epm.lock`, `epm why`, case-insensitive names, a CPM shim
for dependencies that use CPM instead of a manifest, `find_package` version checks.

Known limitations of what exists:

- On Windows a recipe compiles only in the MSVC environment (`INCLUDE`, `LIB`) of a
  developer command prompt (§2): a Visual Studio consumer configured outside one, which
  `get_epm.cmake` otherwise supports (`PoC.md` §8), cannot run an `epm.cpp`.
- A released archive's epm compiles recipes with the compiler CI built it with, at
  CI's path, against archives from that compiler: where that path does not exist it
  cannot run an `epm.cpp`. `get_epm.cmake` therefore never downloads one and builds epm
  on the machine that uses it (`PoC.md` §8).
- A `get_epm.cmake` tool entry `tool/v<x.y.z>/` is keyed by its tag only, not by
  compiler: the first configure that builds it fixes the compiler of its recipe
  libraries. A second project on the same machine built with another compiler family
  stops at `epm_add_recipe`'s compiler check (§2) until the entry is deleted (or
  `EPM_RECIPE_ALLOW_COMPILER_MISMATCH` is set). The configure itself is unaffected:
  epm compiles the recipes it runs with its own compiler.
- The `add_library` wrapper of §9 collides with a dependency that wraps it too.
