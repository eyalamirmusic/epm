# epm graphs — the manifest, the provider and the resolved graph

This document is the contract for the second stage of epm: dependency graphs declared
in a C++ manifest (`epm.cpp`, a recipe the binary compiles and runs), consumed through
CMake's dependency provider, resolved once per configure by the binary. It supersedes the per-call `epm_add_package` flow of
`Docs/PoC.md` §1–§3; the cache layout, fingerprint, config mapping and child build of
`Docs/PoC.md` stay as they are except where this document says otherwise.

Principles:

- CMake keeps only `find_package` and `target_link_libraries`. Every declaration,
  option, config and policy lives in `epm.cpp`; every decision is taken in the binary.
- Source mode is the definition of a package; a binary entry is a cache of building
  that definition. A package's identity is exactly what source mode receives: git url,
  resolved commit, options, configs. Nothing else may influence a binary.
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
struct App final : Recipe
{
    void describe(Project& project, const Env& env) override
    {
        setOption(project.add("fmt", "fmtlib/fmt", "11.0.2"), "FMT_TEST", "OFF");
        auto& b = addConfig(project.add("B", "org/B", "v2.1.0"), "Release");

        if (env.profile == "ci")
            setMode(b, "binary-only");
    }
};
} // namespace

EPM_RECIPE(App)
```

Source fallback and local checkouts never touch CMake code:

```bash
cmake -B build -DEPM_MODE=source            # every package from source
cmake -B build -DEPM_fmt_MODE=source        # one package from source
cmake -B build -DEPM_B_SOURCE=~/Code/B      # a local checkout, added as a subdirectory
cmake -B build -DEPM_PROFILE=ci             # env.profile for the recipes
```

The same names work as environment variables (§3 has the limits).

## 2. Manifest `epm.cpp`

The manifest is a recipe: a C++ file named `epm.cpp` beside the top-level
`CMakeLists.txt` of the consumer, and of every package that has dependencies of its
own. The epm binary compiles it, loads it into its own process and asks it for an
`epm::Project` (`Recipe/Project.h`); the project may also build it, for its IDE
and diagnostics only (`epm_add_recipe`, below).

- `#include <epm/Recipe/Recipe.h>`, then `using namespace epm;`, derive from
  `Recipe` and override `describe(Project&, const Env&)`. `EPM_RECIPE(Type)`, once
  per file at global scope, names the class: it defines the module's one entry point.
  The directive brings in `Recipe`, `Project`, `Env`, `ManifestPackage`, the four
  setters, `projectError`, `findPackage` and `expandGitUrl` (plus whatever other
  `epm/` headers the recipe includes); a recipe's own type or function of one of
  those names is ambiguous with it.
- `project.add(name, git, tag)` appends a dependency (`epm::ManifestPackage`,
  `Spec/Manifest.h`) and returns it; the reference is valid until the next `add`.
  `setOption(package, key, value)`, `addConfig(package, config)`,
  `setMode(package, mode)` and `setAbiMatch(package, abiMatch)` set one field and
  return the package, so calls chain. Unset fields are empty; the binary applies
  defaults.

| Field      | Values                                                            |
|------------|-------------------------------------------------------------------|
| `name`     | the `find_package` name                                           |
| `git`      | `owner/repo`, `https://host/path.git`, `git@host:path.git`, a path |
| `tag`      | a branch, tag or commit                                           |
| `options`  | `KEY` → `VALUE`, set as plain variables in source mode            |
| `configs`  | `Debug`, `Release`, `RelWithDebInfo`, `MinSizeRel`, or `auto`     |
| `mode`     | `auto`, `prebuilt`, `binary-only`, `source`                       |
| `abiMatch` | `strict`, `compatible`, `any`                                     |

- The order of the `add` calls is kept: it is the declaration order of §5 rule 3.
- `name` is case-sensitive (as the PoC). A later stage may relax this. A package
  without a name is an error, and so is a name added twice to one project
  (`projectError`); the error names the package whose `epm.cpp` it is.
- `git` without `://`, `git@` or a leading `/` is a GitHub `owner/repo` shorthand.
- `configs` names are the consumer-facing names; the binary collapses them as
  `epm.cmake` does today (§3 of `PoC.md`).
- The root project may add a package with only `options` (or `configs`, `mode`,
  `abiMatch`), as `project.add("fmt")`: `git` and `tag` then come from the first
  dependency that declares it, and the configure fails if none does.
- `env` is the consumer's `env.json` (`Spec/Env.h`): generator, build type,
  compiler, `systemName`, `systemProcessor`, deployment target and the rest, plus
  `profile`. Every recipe in the graph, the root's and each dependency's, is described
  for the same `env`. `epm describe` without `--env` uses an empty one.
- A recipe may use anything `epm-core` and `eacp-core` offer (`eacp::FilePath`,
  `eacp::Strings`, …): the module resolves those symbols against the epm executable.
- Stores and lock files are not part of this stage.

Profiles are code. `EPM_PROFILE` (a CMake variable, else the environment) reaches the
recipes only as `env.profile`, and a recipe branches on it like on any other `env`
field. epm gives the name no meaning of its own and keeps it out of every fingerprint;
what a recipe does with it (an option, a config, a mode) is what counts.

How the binary runs a recipe (`Recipe/RecipeBuild`, `Recipe/RecipeLoader`,
`Graph/ManifestLoader`):

1. `buildRecipe` compiles it with one direct compiler call, no CMake, into a shared
   module: the compiler, C++ standard, flags, sysroot, architectures, deployment target
   and `epm-core`'s include directories and definitions that built epm itself are baked
   into the binary (`Recipe/RecipeSdk.cpp.in`), so the module shares epm's ABI. The
   module links nothing: its undefined symbols bind at load time to the epm executable,
   which exports `epm-core` and every static library under it
   (`epm_export_recipe_api` in `Lib/epm/CMakeLists.txt`).
2. Modules are cached in `<cache>/recipe/<key12>/epm-recipe.dylib` (`.so` on Linux)
   with `compile.log` beside it. The key hashes the SDK, the epm executable itself and
   the recipe's source, so an unchanged recipe is compiled once per epm binary.
3. `loadRecipe` loads it, checks its `recipeApiVersion` and creates the `Recipe`;
   `describe(project, env)` runs once, and the module is closed again (a `Project` is
   plain data).
4. A recipe that does not compile, does not load or throws fails the configure. The
   error names the package, the log and the compiler's first diagnostic:
   `the manifest of B (<src>/epm.cpp): the recipe <src>/epm.cpp did not compile
   (/usr/bin/c++ exited with code 1); see <cache>/recipe/<key12>/compile.log`, then
   the first `error:` line on its own line.

`epm describe [<epm.cpp>] [--module <path>] [--env <env.json>] [--cache <dir>]` prints
the project a recipe describes, as JSON, for checking one by hand. `--module` loads that
module (`epm_add_recipe`'s, below) instead of compiling the file, so the positional is
then only informational and may be left out; a module that fails to load is named in
the error, with the hint that it may come from another compiler than epm's. `configure`
has no such option: it always compiles its own.

### The recipe as a target of the project: `epm_add_recipe`

```cmake
enable_testing()
epm_add_recipe(AppRecipe)            # [SOURCE <file>] [NO_TEST]
```

`epm_add_recipe` (`epm.cmake`) adds `epm.cpp` (or `SOURCE`, relative to the current
source directory) to the project's own build as the `MODULE` library `AppRecipe`, so the
IDE indexes it with epm's headers and a compile error shows in the normal build. It is
compiled as `buildRecipe` compiles it: linked to `epm::recipe`, at epm's C++ standard
and extensions, hidden visibility, no IPO, `-undefined dynamic_lookup` on Apple,
`-fno-gnu-unique` under GCC, named `AppRecipe.epm-recipe.<dylib|so>` (no `lib`
prefix). The configure never loads this module; it still compiles and runs the file
into its own cache. The custom target `AppRecipe-describe` (folder `epm`, not built by
default) builds the module and runs `epm describe <file> --module
$<TARGET_FILE:AppRecipe> --env <build>/epm/env.json`, printing the project. Unless
`NO_TEST` (or `BUILD_TESTING` is OFF) the test `AppRecipe` runs the same command; ctest
sees it once the project calls `enable_testing()` (or `include(CTest)`), and it needs
the module built first.

**Debugging a recipe.** Build the Debug configuration, so the module has debug info,
then debug the `AppRecipe` test from CLion's CTest integration, or by hand:
`lldb -- epm describe epm.cpp --module <build>/AppRecipe.epm-recipe.dylib --env
<build>/epm/env.json`. Breakpoints in `describe` bind when the module loads. The module
the configure runs is still epm's own cached one, so what you debug is what the
configure executes only when both are compiled from the same file.

`epm::recipe` is an `INTERFACE IMPORTED GLOBAL` target defined once per configure from
`epm recipe-sdk --out <build>/epm/recipe-sdk.cmake`, which writes what the binary has
baked in (`Recipe/RecipeSdk.h`, `Emit/RecipeSdkCMake`) as CMake variables:
`EPM_RECIPE_CXX_COMPILER`, `EPM_RECIPE_COMPILER_ID`, `EPM_RECIPE_CXX_STANDARD`,
`EPM_RECIPE_CXX_EXTENSIONS` and the lists `EPM_RECIPE_INCLUDE_DIRS`,
`EPM_RECIPE_COMPILE_DEFINITIONS` and `EPM_RECIPE_COMPILE_OPTIONS`. The target carries
the include directories (as system directories, like any imported target's), the
definitions, `cxx_std_<N>`, and epm-core's warning flags only under a compiler of
`EPM_RECIPE_COMPILER_ID`. So it works the same with an installed epm as with one built
from a checkout: the SDK always comes from the binary.

- A project compiler other than `EPM_RECIPE_CXX_COMPILER` (compared by real path) gets
  one status line; the target still builds, with the project's compiler.
- When an SDK include directory does not exist (a binary built on another machine),
  `epm_add_recipe` does nothing beyond a status line, as on Windows.
- It does nothing in a project that is not the top-level one (a package added from
  source), and a child build epm runs gets a no-op stub from `epm-export.cmake`, so a
  package may call it for its own developers.

Limits:

- epm must be built by the compiler it compiles recipes with, and that compiler and
  the headers it was built against (epm's and eacp's, at their build-time paths) must
  still be where they were. Released binaries are built on CI, whose paths do not
  exist on a user's machine, so they cannot run recipes yet: a known gap. An epm built
  locally (`EPM_SOURCE_DIR`, or `get_epm.cmake` building one into the cache) can.
- macOS and Linux only. On Windows the configure fails at the first recipe.
- A recipe runs inside the epm process: a crash in it ends the configure.

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
  "packages": {
    "<name>": { "mode": "", "source": "/abs/path", "configs": [], "options": {} }
  }
}
```

CMake collects `EPM_MODE`, `EPM_CONFIGS`, `EPM_ABI_MATCH`, `EPM_STRICT`,
`EPM_BUILD_MISSING_BINARIES` and every `EPM_<Name>_MODE`, `EPM_<Name>_SOURCE`,
`EPM_<Name>_CONFIGS`, `EPM_<Name>_OPTIONS` (a `;`-list of `KEY=VALUE`, a bare `KEY`
meaning `ON`) it finds among normal and cache variables. Empty values are left out;
`EPM_<Name>_SOURCE` is made absolute against `CMAKE_SOURCE_DIR` (a leading `~` is the
home directory).

- `EPM_PROFILE` is not an override: `epm_write_env` puts it into `env.json` as
  `profile`, for the recipes (§2).
- `configs` stays empty unless the user set `EPM_CONFIGS`. `["auto"]` is an explicit
  request for the PoC's `auto` rule and beats the manifest.
- `buildMissingBinaries` is optional: written only when `EPM_BUILD_MISSING_BINARIES`
  is set; unset means on.
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
own: it already shaped the projects through `env.profile` (§2).

The binary records, for every effective value, which source set it (`override`,
`override:package`, `root`, `manifest:<declarer>`, `default`, or
`source dependency <name>` for rule 6 of §5);
`epm why <name>` prints that (later; the data is kept in `graph.json` now).

## 5. Graph rules

1. The root recipe is run. Every declared package's source is fetched into the
   source cache (or taken from `source`). If the checkout holds an `epm.cpp`, it is
   run too (§2) and its packages are added to the graph, recursively; a checkout
   without one has no dependencies. Discovery therefore happens before configure
   continues, by running recipes in the binary, not by executing CMake.
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
5. Packages whose git urls name the same repository (`repositoryKey`: scheme, user,
   trailing `/` and `.git` dropped, lower case) are reported in the same notes list,
   so `strict` fails on them too.
6. A package in `source` mode forces every package that depends on it into `source`
   mode (a binary cannot link a target that only exists in the consumer's build).
7. Packages are resolved in dependency order, so a package's dependencies have cache
   entries before its own child build runs. A package whose dependency ended up in
   source after all (an `auto` fallback) goes to source too.
8. `binary-only` fails the configure on a cache miss. `prebuilt` builds a missing
   entry. `auto` builds a missing entry unless `buildMissingBinaries` is off, in which
   case it falls back to source. `source` never consults the binary cache.

## 6. The binary: `epm configure`

```
epm configure --manifest <epm.cpp> --env <env.json> --overrides <overrides.json>
              --out <dir> [--cache <dir>] [--verbose]
```

Runs the root recipe for the `env.json` given, then every dependency's (§2, §5), and
writes into `<dir>`:

- `graph.cmake` — the response the provider reads (§7).
- `graph.json` — the resolved graph: every package's effective request, source
  directory, commit, mode, entry path, fingerprint, direct dependencies, and the
  provenance of each value. Nested child builds and `epm why` read it.

`epm resolve` (the per-package PoC command) stays until `epm_add_package` is removed.

## 7. Response `graph.cmake`

```cmake
set(epm_PACKAGES "fmt;C;B")                     # dependency order
set(epm_fmt_MODE prebuilt)                      # prebuilt | source
set(epm_fmt_SOURCE_DIR "/cache/src/fmt/<commit>")
set(epm_fmt_DEPS "")                            # direct dependencies, names
set(epm_fmt_OPTIONS "FMT_TEST=OFF")             # effective options, KEY=VALUE list
# prebuilt only:
set(epm_fmt_PREFIX "/cache/bin/fmt/<platform>/<fp12>")
set(epm_fmt_TARGETS_FILE "${epm_fmt_PREFIX}/epm-targets.cmake")
set(epm_fmt_PLATFORM "macos-arm64")
set(epm_fmt_CONFIGS "Release")
set(epm_fmt_NOTE "")                            # ABI note as today
set(epm_NOTES "fmt: B declares 10.2.1, root declares 11.0.2")
```

## 8. CMake: `get_epm.cmake` as the top-level include

`get_epm.cmake` keeps its bootstrap (find, download or build the binary) and is now
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
today, plus `profile`) and `overrides.json` (§3), runs `epm configure` (failing the
configure on a non-zero exit, printing its output), includes `graph.cmake` and prints
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
- The facade writer (`Build/Harvest`) reads every config's `.aliases` and emits,
  after the facade targets, a guarded `add_library(<alias> ALIAS <target>)` for each
  (`if (NOT TARGET <alias>)`).
- `clearConfig` deletes `.aliases` with the config's other files. Entries built
  before this have no `.aliases`, and so no aliases, until rebuilt.

## 10. Nested child builds

`Build/ChildBuild` passes to the child configure of a package that has dependencies,
in addition to today's flags (a package without dependencies gets today's flags
only):

```
-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=<builddir>/epm-export.cmake;<builddir>/epm-nested.cmake
-DEPM_NESTED=ON
-DEPM_GRAPH=<builddir>/epm-graph.cmake
-DEPM_EXECUTABLE=<self>
```

The parent writes all three files into the child's build directory before it
configures. `epm-graph.cmake` is §7's `graph.cmake` for the package's dependency
closure only (`dependencyClosure` in `Graph/Graph.h`), in dependency order, without
notes. `epm-nested.cmake` is `CMake/epm-provider.cmake`, embedded in the binary like
`epm-export.cmake` (`Build/EmbeddedScripts.cpp.in`), followed by
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
links it. `EPM_ABI_MATCH` policies include `deps` in the compared set at every level.
The fingerprint `schema` stays 2; entries made now get new `fp12` names anyway,
because the canonical JSON now has `deps`.

A package's `epm-targets.cmake` does not re-export its dependencies' targets: the
export skips imported targets already, and the provider loads `epm_X_DEPS` first.
`export()` accepts links to imported targets outside the export set and writes them
by the imported target's own name, so an exported library linking `Leaf::Leaf` (an
alias of Leaf's facade) carries `INTERFACE_LINK_LIBRARIES "Leaf"`, which the
consumer's provider has defined by then. `Tests/GraphNested.cmake` checks all of
this end to end.

## 11. Out of scope for this stage

Stores and remote caches, `epm.lock`, `epm why`, case-insensitive names, a CPM shim
for dependencies that use CPM instead of a manifest, `find_package` version checks,
recipes on Windows.

Known limitations of what exists:

- Released epm binaries cannot compile recipes: the compiler and headers that built
  them are not on the user's machine (§2).
- The source cache hashes the git url as spelled, so `…/eacp` and `…/eacp.git` are
  different packages there, although §5 rule 5 calls them the same repository.
- The `add_library` wrapper of §9 collides with a dependency that wraps it too.
