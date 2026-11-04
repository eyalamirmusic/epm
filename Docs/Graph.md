# epm graphs — the manifest, the provider and the resolved graph

This document is the contract for the second stage of epm: dependency graphs declared
in a JSON manifest, consumed through CMake's dependency provider, resolved once per
configure by the binary. It supersedes the per-call `epm_add_package` flow of
`Docs/PoC.md` §1–§3; the cache layout, fingerprint, config mapping and child build of
`Docs/PoC.md` stay as they are except where this document says otherwise.

Principles:

- CMake keeps only `find_package` and `target_link_libraries`. Every declaration,
  option, config and policy lives in `epm.json`; every decision is taken in the binary.
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

`epm.json` next to the top-level `CMakeLists.txt`:

```json
{
  "schema": 1,
  "packages": [
    { "name": "fmt", "git": "fmtlib/fmt", "tag": "11.0.2", "options": { "FMT_TEST": "OFF" } },
    { "name": "B", "git": "org/B", "tag": "v2.1.0", "configs": ["Release"] }
  ],
  "profiles": {
    "ci": { "configs": ["Release"], "mode": "binary-only" }
  }
}
```

Source fallback and local checkouts never touch CMake code:

```bash
cmake -B build -DEPM_MODE=source            # every package from source
cmake -B build -DEPM_fmt_MODE=source        # one package from source
cmake -B build -DEPM_B_SOURCE=~/Code/B      # a local checkout, added as a subdirectory
cmake -B build -DEPM_PROFILE=ci             # select a profile
```

The same names work as environment variables (§3 has the limits).

## 2. Manifest `epm.json`

Miro-reflected `epm::Manifest` (`Spec/Manifest.h`). Field names are the JSON keys.
Unset strings are empty, unset lists are empty; the binary applies defaults.

```json
{
  "schema": 1,
  "packages": [
    {
      "name": "<name>",
      "git": "owner/repo | https://host/path.git | git@host:path.git",
      "tag": "<branch | tag | commit>",
      "options": { "KEY": "VALUE" },
      "configs": ["Debug", "Release"],
      "mode": "auto | prebuilt | binary-only | source",
      "abiMatch": "strict | compatible | any"
    }
  ],
  "profiles": {
    "<name>": {
      "configs": [], "mode": "", "abiMatch": "",
      "packages": [ { "name": "<name>", "options": {}, "configs": [], "mode": "", "abiMatch": "" } ]
    }
  }
}
```

- `packages` is an array, so the file's order is kept: it is the "manifest order" of
  §5 rule 3. A profile's `packages` is an array of the same shape.
- `name` is the `find_package` name, case-sensitive (as the PoC). A later stage may
  relax this. A package without a `name` is an error, and so is a name that appears
  twice in the same `packages` (of the manifest or of one profile; the error names
  it) and a `packages` that is not an array (the earlier name-keyed object shape).
  Inline profiles are checked when the manifest is read, profile files when selected.
- `git` without `://`, `git@` or a leading `/` is a GitHub `owner/repo` shorthand.
- `configs` names are the consumer-facing names (`Debug`, `Release`, `RelWithDebInfo`,
  `MinSizeRel`); the binary collapses them as `epm.cmake` does today (§3 of `PoC.md`).
- The root manifest may name a package with only `options` (or `configs`, `mode`,
  `abiMatch`): `git` and `tag` then come from the first dependency that declares it,
  and the configure fails if none does.
- Stores and lock files are not part of this stage. Unknown keys are ignored.

A profile is a named partial manifest. Its value may instead be the path of a JSON
file holding one profile object (relative to the manifest's directory), for policy
kept outside the repository. An `EPM_PROFILE` that the manifest does not define is
tried as such a path too.

## 3. Overrides `overrides.json`

Miro-reflected `epm::Overrides` (`Spec/Overrides.h`), written by `epm_write_overrides`
(`epm.cmake`) under `${CMAKE_BINARY_DIR}/epm/` from the CMake variables it can see. The
binary fills what the file leaves unset from its own environment (`withEnvironment`);
CMake variables win over environment.

```json
{
  "schema": 1,
  "profile": "ci",
  "mode": "", "configs": [], "abiMatch": "", "strict": false,
  "buildMissingBinaries": true,
  "packages": {
    "<name>": { "mode": "", "source": "/abs/path", "configs": [], "options": {} }
  }
}
```

CMake collects `EPM_PROFILE`, `EPM_MODE`, `EPM_CONFIGS`, `EPM_ABI_MATCH`, `EPM_STRICT`,
`EPM_BUILD_MISSING_BINARIES` and every `EPM_<Name>_MODE`, `EPM_<Name>_SOURCE`,
`EPM_<Name>_CONFIGS`, `EPM_<Name>_OPTIONS` (a `;`-list of `KEY=VALUE`, a bare `KEY`
meaning `ON`) it finds among normal and cache variables. Empty values are left out;
`EPM_<Name>_SOURCE` is made absolute against `CMAKE_SOURCE_DIR` (a leading `~` is the
home directory).

- `configs` stays empty unless the user set `EPM_CONFIGS`. `["auto"]` is an explicit
  request for the PoC's `auto` rule and beats the profile and the manifest.
- `buildMissingBinaries` is optional: written only when `EPM_BUILD_MISSING_BINARIES`
  is set; unset means on.
- `EPM_STRICT` from the environment can only turn strict on, since a CMake `OFF`
  cannot be told from an unset one.
- `EPM_<Name>_*` environment variables are read only for names in the graph.

## 4. Merge order

Each package ends up as one effective `Request` (today's `Spec/Request.h`, extended by
`deps`). Values are taken from the first source that sets them, in this order:

1. Overrides, per package (`EPM_<Name>_*`), then global (`EPM_MODE`, …).
2. The selected profile, per package, then its globals.
3. The root manifest's package entry.
4. The manifest of the dependency that declared the package (graph rules, §5).
5. Defaults: `mode=auto`, `configs=auto` (the PoC rule), `abiMatch=compatible`.

Options merge by key, with the same precedence per key. `source` is only an override
and makes the package `mode=source` with that directory instead of a checkout.
`buildMissingBinaries` is only an override (default on).

The binary records, for every effective value, which source set it (`override`,
`override:package`, `profile:<name>`, `profile:<name>:package`, `root`,
`manifest:<declarer>`, `default`, or `source dependency <name>` for rule 6 of §5);
`epm why <name>` prints that (later; the data is kept in `graph.json` now).

## 5. Graph rules

1. The root manifest is read. Every declared package's source is fetched into the
   source cache (or taken from `source`). If the checkout holds an `epm.json`, its
   packages are added to the graph, recursively. Discovery therefore happens before
   configure continues, by reading manifests, not by executing CMake.
2. Git and tag are not merged: every declaration, the root's included, is a
   requirement, and minimal version selection over them (`Docs/Resolution.md`)
   chooses one version, which `epm.lock` records with its commit and Merkle hash.
   Disagreeing semantic version tags take the highest; any other disagreement stops
   the configure, naming each declarer, until the root's `epm.json` `overrides`
   settles it. A declaration the walk meets after the package was fetched that
   changes the choice is written to the lock and asks for one more configure.
3. For every other field the root wins: a package declared by the root keeps the
   root's options, configs, mode and abiMatch whatever a dependency declares. Among
   dependencies, the first declaration in a depth-first walk of the root's packages
   in manifest order (the order of each `packages` array) wins. Within one package,
   each declared dependency's subtree is walked before the package's own later
   declarations, as CPM's first add would see them.
4. Every later declaration that sets one of those fields (options, configs, mode,
   abiMatch) to something other than the winning value is reported as a note naming
   both declarers: `fmt: B declares mode source, root leaves it unset`,
   `fmt: B declares option FMT_TEST=ON, C declares option FMT_TEST=OFF`. `strict`
   turns the notes into an error and the configure fails.
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
epm configure --manifest <epm.json> --env <env.json> --overrides <overrides.json>
              --out <dir> [--cache <dir>] [--verbose]
```

Writes into `<dir>`:

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
3. If there is no `epm.json` next to the top-level `CMakeLists.txt`, stop; the
   project may still call `epm_add_package`.
4. Fail clearly if this file was `include()`d (after `project()`) rather than listed
   in `CMAKE_PROJECT_TOP_LEVEL_INCLUDES`, or if the found epm is too old to have
   `epm_use_manifest`.
5. `epm_use_manifest(<epm.json>)` registers the dependency provider
   (`cmake_language(SET_DEPENDENCY_PROVIDER … SUPPORTED_METHODS FIND_PACKAGE
   FETCHCONTENT_MAKEAVAILABLE_SERIAL)`; CMake allows that only at `project()` time)
   with `epm_configure_graph` as its one-time preparation.

`epm configure` runs lazily, at the provider's first call (the first `find_package`
or `FetchContent_MakeAvailable`), not at `project()`, so a `CMAKE_CXX_STANDARD` set
after `project()` still reaches `env.json`. That first call writes `env.json` (as
today) and `overrides.json` (§3), runs `epm configure` (failing the configure on a
non-zero exit, printing its output), includes `graph.cmake` and prints its notes once.
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

Stores and remote caches, `epm why`, case-insensitive names, `find_package` version
checks. The lock and the CPM and FetchContent capture for dependencies without a
manifest are in `Docs/Resolution.md`.

Known limitations of what exists:

- The lock compares git urls as spelled, so `…/eacp` and `…/eacp.git` from two
  declarers are a conflict, although §5 rule 5 calls them the same repository.
- The `add_library` wrapper of §9 collides with a dependency that wraps it too.
