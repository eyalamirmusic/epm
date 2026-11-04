# epm — design plan

A C++ package manager for CMake projects. Written in C++ on top of eacp, driven from
CMake through a thin module, inspired by cpm.cmake for the user-facing shape, and adding
what CPM deliberately leaves out: resolved pins, a sound source cache, prebuilt binaries,
and local/remote cache repositories.

Status: the local-cache proof of concept is implemented (`Docs/PoC.md`), and so is the
manifest and dependency-graph stage that supersedes its per-call flow (`Docs/Graph.md`).

---

## 1. Goals and non-goals

### Goals

1. **CPM-shaped source dependencies.** `epm_add_package(...)` accepts CPM's argument set
   and shorthand (`gh:user/repo@1.2.3`, `gh:user/repo#sha`, URL archives), fetches the
   source, and `add_subdirectory`s it. Migrating a project from CPM is a mechanical rename.
2. **Correct caching.** Sources are cached by *resolved content* (commit SHA or archive
   SHA-256), with completeness markers, atomic moves and cross-process locks. Branch and
   tag names are resolved once and pinned in a lockfile.
3. **Prebuilt / binary resources**, two flavours sharing one store:
   - *Built by epm*: a dependency configured, built and installed in isolation into a
     prefix, archived, and keyed by `source-id + build-config-id + options + dependency keys`.
   - *External prebuilts*: SDKs and vendor libraries shipped as per-platform archives
     (onnxruntime, whisper, WebView2, tflite), wrapped into imported targets.
4. **Cache repositories.** The local cache directory *is* a repository. The same layout can
   live on a network share, a plain HTTP(S) host, S3-style storage or GitHub Releases, and
   be used read-only by developers and read-write by CI, for both sources and binaries.
5. **Stays compatible with the existing workspace convention** (`CPM_<Name>_SOURCE`
   overrides, flat sibling checkouts, `workspace.json`) and with dependencies that still
   vendor CPM.cmake internally.

### Non-goals (v1)

- Semver range solving. Versions are exact, as in CPM and pkgcpp. "First wins" with loud
  diagnostics; a strict mode turns mismatches into errors.
- A central package registry/index. Packages are addressed by URL, as in CPM.
- Replacing CMake. epm never generates build files for the consumer; it hands CMake either a
  source directory or an install prefix.
- Reproducing Conan/vcpkg recipes. A package that already has a good `CMakeLists.txt` and
  `install(EXPORT)` needs no recipe. A small recipe is needed only for external prebuilts and
  for libraries without install rules.

---

## 2. Lessons taken from the surveys

**From cpm.cmake** (what to keep, what to fix):

| Keep | Fix |
|------|-----|
| Argument names, shorthand URIs, `<name>_SOURCE_DIR/_BINARY_DIR/_ADDED` outputs | Cache hit = "directory exists" (no completeness marker; interrupted downloads corrupt the cache) |
| `OPTIONS "KEY VALUE"` set as plain vars with CMP0077 before `add_subdirectory` | Patch *paths* hashed, not patch *contents* |
| `CPM_<Name>_SOURCE` local override | Branch tags never update once cached; lock records tag names, not commits |
| find_package redirect via `CMAKE_FIND_PACKAGE_REDIRECTS_DIR` | Serial configure-time fetching, no prefetch, no parallelism |
| Lock file as flat override list of declarations | No binaries, no remote cache, no offline/frozen mode, no auth story |
| Policies forced NEW (CMP0077/0126/0135/0150) | Case-sensitive NAME leading to `cpmNames` hacks in workspaces |

**From pkgcpp** (the user's earlier attempt): content-addressed `sources/sha256/<hex>` +
`extracted/sha256/<hex>` layout, atomic temp-then-rename downloads, lockfile with sha256,
bootstrap-build of the tool at configure time, `resolution.cmake` handed back to CMake. All
of these carry over. What it lacked: transitive deps, binaries, remotes, CPM-style ergonomics.

**From tamber-3rd-party**: one place owns versions and the source-vs-prebuilt choice; every
prebuilt has a `<DEP>_FROM_SOURCE` switch; prebuilt and source modes expose the *same* target
names. These become first-class epm features.

**From vcpkg / Conan / Hunter / Nix / Bazel**:

- A prebuilt static library's key must include: content identity of the source and patches;
  content hash of the compiler binaries (not mtime or version string); target triple,
  SDK/deployment target, sysroot; build type, C++ standard, stdlib, MSVC runtime, PIC, LTO,
  sanitizers, effective flags and defines; package options; **each dependency's full binary
  key** (static libs embed dependency headers, so Conan's `full_mode` is the right default);
  an allow-list of env vars; CMake version; and a key-schema version.
- Store the manifest that produced each key next to the artifact so misses can be diffed
  (vcpkg `vcpkg_abi_info.txt`, Hunter `toolchain.info`).
- Simplest sound remote: immutable content-addressed blobs over `HEAD/GET/PUT`, metadata
  signed (ed25519, trusted-keys list) so trust lives in content, not transport. CI writes,
  developers read.
- Regrets to avoid: unhashed host compiler, mtime checks, absolute paths baked into
  binaries, dependence on vendor CI caches or vendor CLIs, "latest" by upload time, mutable
  entries, unversioned index formats.

---

## 3. Architecture overview

```
 consumer CMakeLists.txt
   include(CMake/epm.cmake)              # thin module, vendored like CPM.cmake
   epm_add_package("gh:eyalamirmusic/eacp#<sha>" OPTIONS ...)
          │
          │ execute_process(epm resolve --request <json> --response <cmake>)
          ▼
 ┌─────────────────────────────── epm (C++ CLI, eacp-core + eacp-network) ──┐
 │ Spec      parse CPM-style args / shorthand → PackageRequest               │
 │ Resolve   tag/branch → commit (git ls-remote), URL → sha256; lockfile     │
 │ Source    git snapshot / archive download → source cache                  │
 │ Binary    toolchain probe, binary key, isolated build, package, unpack    │
 │ Store     CAS blobs + index; local dir, file://, http(s), GitHub Releases │
 │ Lock      epm.lock read/write, prefetch manifest, frozen mode             │
 │ Emit      response .cmake (vars, mode, prefix, imported-target shim)      │
 └───────────────────────────────────────────────────────────────────────────┘
          │
          ▼
   include(response.cmake)   →  add_subdirectory(...)      (source mode)
                             or find_package(X CONFIG BYPASS_PROVIDER)  (prebuilt mode)
```

Everything that *must* happen inside the CMake process stays in CMake: setting `OPTIONS`
variables, `add_subdirectory`, `find_package`, creating imported targets, policies. Everything
else (network, hashing, locking, extraction, building in isolation, lockfile) is C++.

### 3.1 Why a per-call process, and how to avoid CPM's serial slowness

The dependency graph is discovered by *executing* CMake: a dependency's own `epm_add_package`
calls are only seen once it is added. So the primitive has to be a per-call resolve. Process
spawn is cheap; a cache hit is a lock-free read of a small JSON marker.

The slow part (network) is moved out of the per-call path with the **lockfile as prefetch
manifest**: on configure, before the first add, the module runs `epm prefetch epm.lock` once.
It fetches every locked source and every binary matching the current build-config-id, in
parallel. Per-call resolves then all hit. First configure of a brand-new project is serial
like CPM; every configure after `epm.lock` exists, and every CI run, is parallel.

---

## 4. CMake interface

### 4.1 Entry points

```cmake
include(CMake/epm.cmake)                       # bootstraps the epm binary (see 4.4)
epm_use_lock(${CMAKE_SOURCE_DIR}/epm.lock)      # optional; enables prefetch + frozen mode
epm_add_stores(...)                             # optional; project-level cache repositories

epm_add_package("gh:fmtlib/fmt@11.0.2")          # shorthand, like CPM
epm_add_package(
  NAME eacp
  GITHUB_REPOSITORY eyalamirmusic/eacp
  GIT_TAG 3f1c…                                  # commit preferred; tags/branches get pinned in the lock
  OPTIONS "EACP_BUILD_GRAPHICS OFF"
  MODE auto                                      # source | prebuilt | auto   (new)
  TARGETS eacp::core eacp::network               # names that must exist in either mode (new)
)
epm_add_prebuilt(                                # external binary resource (new)
  NAME onnxruntime VERSION 1.19.2
  URL  macos-arm64  https://…/onnxruntime-osx-arm64-1.19.2.tgz  SHA256=…
  URL  windows-x64  https://…/onnxruntime-win-x64-1.19.2.zip    SHA256=…
  RECIPE CMake/recipes/onnxruntime.cmake         # creates imported targets from the prefix
)
```

Accepted CPM arguments, unchanged in meaning: `NAME VERSION GIT_TAG GIT_REPOSITORY
GITHUB_REPOSITORY GITLAB_REPOSITORY BITBUCKET_REPOSITORY URL URL_HASH SOURCE_DIR SOURCE_SUBDIR
DOWNLOAD_ONLY EXCLUDE_FROM_ALL SYSTEM OPTIONS PATCHES GIT_SHALLOW GIT_SUBMODULES FORCE
CUSTOM_CACHE_KEY`. Shorthand URIs (`gh: gl: bb:`, `@version`, `#tag|hash`, `#SHA256=…`) and
their implied `EXCLUDE_FROM_ALL SYSTEM` match CPM. Outputs: `<name>_SOURCE_DIR`,
`<name>_BINARY_DIR`, `<name>_ADDED`, plus new `<name>_MODE` (`source|prebuilt`) and
`<name>_PREFIX` (prebuilt install prefix).

Also: `epm_declare_package(Name args…)` (first declaration wins, as in CPM) and
`epm_get_package(Name)`.

### 4.2 Local overrides and the workspace convention

Resolution order per package: already added → `EPM_<Name>_SOURCE` or `CPM_<Name>_SOURCE`
(variable or env) → declaration/lock → stores → origin. Name matching is **case-insensitive**
with a warning on spelling mismatch, so one override line serves `eacp` and `EACP`
(the reason `cpmNames` exists today). Workspaces keep working without changes.

### 4.3 Transitive dependencies that still use CPM

Dependencies vendor their own `CPM.cmake` and call `CPMAddPackage`. epm's module defines a
`CPMAddPackage` / `CPMDeclarePackage` / `CPMFindPackage` shim that forwards to
`epm_add_package`, and sets CPM's include guard so that a nested `include(CPM.cmake)`
returns early without redefining the functions. Verified against CPM 0.40.2 and 0.43.2
(identical logic): the module must set the GLOBAL property `CPM_INITIALIZED`, the
`CACHE INTERNAL` variable `CPM_DIRECTORY` to the epm module's directory (any path other than
the nested copy's), and `CPM_VERSION` to a high value such as `99.0.0` so the "dependency is
using a more recent CPM" author warning never fires. With those set, the nested file hits
`if(CPM_INITIALIZED) return()` before any `function(CPMAddPackage …)`. Result: the whole
transitive tree is resolved, cached and locked by epm with zero changes to dependencies.

Later (phase 5): a CMake **dependency provider** (`cmake_language(SET_DEPENDENCY_PROVIDER …)`
in CMake ≥ 3.24, injected through `CMAKE_PROJECT_TOP_LEVEL_INCLUDES`) intercepts raw
`find_package` and `FetchContent_MakeAvailable` too.

### 4.4 Bootstrapping the binary

`epm.cmake` locates the tool in this order: `EPM_EXECUTABLE` → `<cache>/tool/<version>/epm`
→ download the release binary for this host from GitHub Releases, verify the SHA-256 embedded
in `epm.cmake`, store in the cache → fall back to building epm from source into the cache
(pkgcpp approach; one-time cost per machine; the tool's own dependencies never enter the
consumer's build). The module pins an epm version the same way CPM.cmake pins itself.
`get_epm.cmake` mirrors `get_cpm.cmake` for projects that do not want to vendor the module.

### 4.5 Build configuration handed to the tool

Once per configure, the module writes `<build>/epm/env.json`: compiler paths and ids,
`CMAKE_SYSTEM_NAME/PROCESSOR`, `CMAKE_OSX_ARCHITECTURES`, `CMAKE_OSX_DEPLOYMENT_TARGET`,
`CMAKE_SYSROOT`, Android/iOS variables, `CMAKE_BUILD_TYPE` or `CMAKE_CONFIGURATION_TYPES`,
`CMAKE_CXX_STANDARD`, `CMAKE_CXX_FLAGS(_<CONFIG>)`, `CMAKE_MSVC_RUNTIME_LIBRARY`,
`CMAKE_POSITION_INDEPENDENT_CODE`, `CMAKE_INTERPROCEDURAL_OPTIMIZATION`, sanitizer flags,
generator, CMake version, toolchain-file path. The tool turns this into a `build-config-id`
(section 6.1).

---

## 5. Sources

### 5.1 Identity

`source-id = sha256(canonical JSON{ kind, origin, resolved, patches[], submodules })` where
`resolved` is the full commit SHA (git) or the archive SHA-256 (URL), and `patches[]` are
*content* hashes. NAME, VERSION and OPTIONS are not part of it (same as CPM), so two consumers
spelling the same source differently share one checkout.

Tags and branches are resolved to a commit with `git ls-remote` at lock time and recorded in
`epm.lock`. `epm update [name]` re-resolves. In frozen mode nothing is ever re-resolved.

### 5.2 Fetching

- **git**: `git init && git fetch --depth 1 <url> <sha> && git checkout FETCH_HEAD`, plus
  `git submodule update --init --depth 1` for requested submodules. Fallback to a full fetch
  when the server refuses shallow-by-SHA. Shell out to the `git` CLI (as CPM, FetchContent
  and AgentWorkspace do); lift AgentWorkspace's `Git` wrapper (non-interactive env, porcelain
  parsers). Private repos use the user's git/ssh credentials, same as today.
- **URL archives**: `HTTP::downloadFile` (resumable, progress) into the CAS, verified against
  `URL_HASH` or hashed-and-pinned when none is given. Extraction via `cmake -E tar` (CMake is
  guaranteed present at configure time and handles zip, gz, bz2, xz, zstd) in v1; eacp `Zip`
  for `.zip` when desired. This avoids adding libarchive/liblzma to eacp.
- **Snapshot policy** (decision to confirm): the cached source is a clean export *without*
  `.git`. Reasons: deterministic, trivially exportable to a remote source cache as a tarball,
  no "dirty cache" heuristics. Developers who want to edit a dependency use a workspace
  override or `epm checkout <name>` (clones a real repo next to the project and sets the
  override). An `EPM_KEEP_GIT` option can be offered if this proves too restrictive.

### 5.3 Local cache layout

```
~/.cache/epm/                      (EPM_CACHE, else XDG_CACHE_HOME/epm, else platform cache dir)
  tool/<version>/epm               bootstrapped tool binaries
  blobs/sha256/<xx>/<hash>         content-addressed archives (sources, binaries, assets)
  src/<name>/<source-id-12>/       extracted source snapshot
  src/<name>/<source-id-12>.json   completeness marker + full manifest (written last, atomically)
  bin/<name>/<binary-key-12>/      extracted install prefix
  bin/<name>/<binary-key-12>.json  completeness marker + key manifest
  index/sources/<source-id>.json   source metadata (origin, resolved, blob)
  index/binaries/<binary-key>.json binary metadata (manifest, blob, size, signature)
  tmp/                             staging; everything lands by rename
  locks/                           IPC::Lock files per name
```

Human-readable `<name>/` directories keep IDE navigation sane (CPM complaint #408). The
directory tree is identical to the remote layout (section 7), so `rsync`, a network share,
or `python -m http.server` on this folder is already a working cache repository.

---

## 6. Binaries

### 6.1 Toolchain probe and build-config-id

The tool computes, from `env.json`, a canonical manifest and hashes it:

- content SHA-256 of the C and C++ compiler executables (resolved through symlinks), plus
  `-dM -E` predefined macros for GCC/Clang and `cl /Bv`-style identity for MSVC
  (catches `_GLIBCXX_USE_CXX11_ABI`, libc++ vs libstdc++, `_ITERATOR_DEBUG_LEVEL`);
- target triple / `CMAKE_SYSTEM_*`, architectures, deployment target, SDK or NDK version,
  sysroot;
- per-config: build type, C++ standard, flags, defines, MSVC runtime, PIC, LTO, sanitizers;
- toolchain-file *content* hash, CMake version, generator family, allow-listed env vars;
- `key-schema-version`.

`build-config-id = sha256(manifest)`; the manifest is stored next to every artifact and
`epm why-miss <name>` diffs two manifests (the local one and each remote candidate's) to
explain a miss.

### 6.2 Binary key

```
binary-key = sha256({ schema, name, source-id, build-config-id,
                      options (sorted, as applied), recipe-hash,
                      deps: { depName: depBinaryKey }  (full mode) })
```

Configurations are not part of the key: one entry holds every configuration built for
that key, and configurations are **additive** — an entry with Release that is asked for
Debug builds only Debug and adds it, never rebuilding what is there. Which ones are built
is `EPM_CONFIGS`: `auto` (the default) builds a single-config consumer's own
`CMAKE_BUILD_TYPE` and Debug + Release for a multi-config generator (Xcode, VS, Ninja
Multi-Config); an explicit list is also accepted. RelWithDebInfo and MinSizeRel are built
as Release, and the generated shim maps every consumer configuration to the same-named
one if present, else Release, else the first available.

Which existing entry a consumer may use is `EPM_ABI_MATCH`: `compatible` (the default:
same platform, compiler id and major version, deployment target, C++ standard, options,
source and toolchain-file contents, so Xcode's and the command-line tools' clang share
entries), `strict` (every recorded field) or `any` (platform, options, source). An exact
match is preferred; a non-exact one is reported on the configure line. In the PoC the
recorded fingerprint is the env-derived subset of 6.1 (`Docs/PoC.md` §3, §9).

### 6.3 Building a package in isolation

`epm build <name>` (invoked automatically in `MODE auto|prebuilt` on a miss when
`EPM_BUILD_MISSING_BINARIES` is on, or by CI explicitly):

1. Resolve the source (section 5).
2. Child configure: `cmake -S <src>/<subdir> -B <cache>/tmp/build-<key> -G <gen>
   --toolchain <file> -D<OPTIONS…> -DCMAKE_INSTALL_PREFIX=<stage> -DEPM_NESTED=ON`
   plus the env's ABI settings. The child includes the same `epm.cmake`, so the package's own
   `epm_add_package` calls recurse in prebuilt mode and populate `CMAKE_PREFIX_PATH` with
   their prefixes. Shared cache, shared lock, shared stores via environment.
3. `cmake --build --config <cfg>` and `cmake --install --config <cfg>`.
4. Relocatability: `-ffile-prefix-map=<src>=.` and `=<stage>=.`; reject config files that
   contain the stage path (check and warn); strip build dir from installed `.pc`/cmake files.
5. Pack the stage as `tar.zst` (decision to confirm vs zip; tar keeps symlinks and modes for
   macOS frameworks) into the CAS, write index entry + manifest, rename into `bin/`.

### 6.4 Consuming a prebuilt

The response `.cmake` prepends `<prefix>` to `CMAKE_PREFIX_PATH` and runs
`find_package(<Name> CONFIG REQUIRED BYPASS_PROVIDER)`. For packages without
`install(EXPORT)` (today: eacp, Miro, ResEmbed, NanoTest, ESIMD, EADataStructures) two
routes: (a) add proper install/export rules upstream, with an `epm_install_targets()`
helper to make that one line (preferred; eacp first); (b) a *recipe* (`RECIPE file.cmake`)
that creates imported targets by globbing the prefix. The `TARGETS` argument lets epm assert
that the same target names exist after either `add_subdirectory` or `find_package`, so
flipping `MODE` never breaks the consumer.

### 6.5 External prebuilt resources

`epm_add_prebuilt` is a package whose "source" is a table `platform-key → URL + SHA256`
(platform-key derived from `env.json`: `macos-arm64`, `macos-universal`, `windows-x64`,
`linux-x64`, `ios-arm64`, `android-arm64-v8a`). The blob lands in the CAS, is extracted to
`bin/<name>/<key>/`, and the recipe produces `IMPORTED` targets with per-config locations.
Binary key here = `sha256(platform-key, archive sha256, recipe-hash)`; no toolchain probe,
since the vendor built it. Works with NuGet packages (`.nupkg` is a zip) to replace eacp's
hand-written `FindWebView2.cmake`, and with git-LFS-free hosting for tamber-3rd-party.

### 6.6 Mode policy

Per package `MODE source|prebuilt|auto`; global `EPM_DEFAULT_MODE`; per-package
`EPM_<Name>_FROM_SOURCE=ON` (tamber convention) forces source. `auto`: local binary → remote
binary → build-and-cache (if allowed) → source `add_subdirectory`. Every decision is logged
in one line per package at configure (quiet by default, `EPM_VERBOSE` for detail; the user
dislikes noisy configure output).

---

## 7. Stores (cache repositories)

One `Store` interface: `has(key)`, `get(key)`, `put(key, blob, meta)`, `list(prefix)`.
Keys are the paths of the layout in 5.3 (`blobs/…`, `index/sources/…`, `index/binaries/…`).

Backends, in order of implementation:

1. **Directory** (`file://`): local disk, network share, Dropbox/Drive folder.
2. **HTTP(S)**: `HEAD/GET/PUT` on `<base>/<key>`, optional bearer token from an env var or a
   credential-helper command (never on argv). Covers nginx+WebDAV, S3/GCS/R2 via presigned or
   public URLs, any static host for read-only mirrors.
3. **GitHub Releases**: assets named by key in a release of a cache repo (Hunter's model);
   fits the user's open-source libraries and GitHub Actions CI.
4. Later: S3 API native, OCI registries (`oras`-style, as Spack does).

Configuration merges `~/.config/epm/config.json` (user), the project's `epm.cpp`
(project), and `epm_add_stores()` in CMake, each store with `read`/`write` flags and
priority. Policies: `EPM_OFFLINE` (no network at all), `EPM_BLOCK_ORIGIN` (stores only, never
upstream git/URL — vcpkg's `x-block-origin`), timeouts and retry counts.

Integrity: blobs are always SHA-256-verified after download. Index entries can carry an
ed25519 signature; `trusted-keys` in config; unsigned entries accepted only from stores
marked `trust: true`. Writers are CI; developers default to read-only.

Commands: `epm cache ls|gc|verify`, `epm cache push <store> [--sources|--binaries]
[--since <lock>]`, `epm cache pull <store>`, `epm publish <name>` (binary + source for the
current config).

---

## 8. Lockfile

`epm.lock` (JSON via Miro reflection), committed with the project:

```json
{ "schema": 1,
  "packages": {
    "eacp": { "request": "gh:eyalamirmusic/eacp#develop",
              "source": { "kind": "git", "url": "https://github.com/eyalamirmusic/eacp.git",
                          "commit": "3f1c…", "patches": ["sha256:…"] },
              "sourceId": "…",
              "binaries": { "<build-config-id>": "<binary-key>" } },
    "fmt":  { "request": "gh:fmtlib/fmt@11.0.2",
              "source": { "kind": "git", "url": "…", "commit": "…" }, "sourceId": "…" }
  } }
```

- Written at the end of every non-frozen configure (all transitive packages, including
  unversioned ones — fixing CPM's "unversioned packages are commented out").
- Read before the first package: acts as declaration list (first wins), prefetch manifest,
  and in `EPM_FROZEN=ON` (CI default) as a hard contract: any request not satisfiable from the
  lock is an error.
- `epm lock` writes without configuring the project (resolves from CMake args by running a
  configure in "collect only" mode); `epm update [name…]` re-resolves tags/branches.
- Added to `CMAKE_CONFIGURE_DEPENDS` so editing it reconfigures.

AgentWorkspace can later read `epm.lock` to know which repos a project needs, replacing
hand-written `cpmNames`.

---

## 9. Conflicts and diagnostics

- Identity is the case-folded NAME. First add wins (CPM semantics, needed for compatibility).
- Unlike CPM, *any* mismatch between the winning package and a later request is reported:
  different source-id, different options, different mode. `EPM_STRICT=ON` turns these into
  errors. The root project resolves conflicts via `epm_declare_package` or the lock.
- Same source-id under two names → warning (duplicate targets are coming).
- `epm why <name>` prints the resolution chain; `epm why-miss <name>` diffs key manifests.

---

## 10. Tool structure (C++)

Layout mirrors AgentWorkspace (`WorkspaceCore` static lib + thin app + NanoTest), following
eacp conventions (Allman, 4 spaces, `Files::` bool returns, Miro for all JSON, eacp
`Toolbox.md` reuse map checked before writing helpers).

```
epm/
  CMake/epm.cmake, get_epm.cmake      consumer module (the only CMake the user vendors)
  CMake/CPM.cmake                     epm's own bootstrap deps until it self-hosts
  Lib/epm/
    Spec/        PackageRequest, shorthand parser, canonical JSON
    Resolve/     tag/branch/URL resolution, lock integration
    Source/      GitFetcher, ArchiveFetcher, Patcher, SourceCache
    Binary/      ToolchainProbe, BinaryKey, IsolatedBuild, Packager, PrebuiltResource
    Store/       Store interface, DirectoryStore, HttpStore, GitHubReleasesStore, Cas, Index
    Lock/        LockFile, Prefetch, FrozenPolicy
    Emit/        CMake response writer, imported-target shim generator
    Util/        Sha256, Tar (via `cmake -E tar`), Semaphore/parallel runner, Log
  Apps/epm/Main.cpp                    Apps::runConsole + subcommand dispatch
  Tests/                               NanoTest; fixture repos/archives; CMake integration tests
```

eacp facilities used directly: `Files::*` (atomic write, move, copy, listing),
`FilePath::cacheDirectory`, `Processes::run` (git, cmake), `HTTP::downloadFile` /
`asyncDownload` (resumable, progress), `OnlineResource` (ETag revalidation for index files),
`IPC::Lock` (cross-process cache locks), `Zip::Reader`, Miro JSON, `Args::Parser`,
`Console`, `LOG`.

Gaps to fill (prefer upstreaming to eacp where general):

| Need | Plan |
|------|------|
| SHA-256 | add `eacp::Hash::sha256` to Core (promote the private SHA-1 alongside) |
| tar/gz/xz/zst | shell out to `cmake -E tar` in v1; native later if needed |
| subcommands in `Args::Parser` | add `Parser::subcommand(...)` upstream |
| `split`/`join` strings | wait for eacp #177 or local shim |
| parallel fetch | small worker pool over `Threads::Thread` + `TaskSemaphore` |
| PATH lookup, `wait(timeout)` | small additions to `Processes` |
| log levels | local `Log` wrapper over `LOG` with quiet/normal/verbose |
| git | lift `AgentWorkspace/Lib/WorkspaceCore/Git.*` |
| ed25519 (phase 5) | small vendored implementation or platform crypto |

Self-hosting: epm's own build fetches eacp/Miro/NanoTest with CPM until phase 1 is stable,
then switches to `epm.cmake` itself (a release binary bootstraps the build of the next one).

---

## 11. Phases

**Phase 0 — scaffold (1 week)**
Repo, eacp bootstrap via CPM, NanoTest harness, CI matrix (macOS/Windows/Linux). Release
pipeline that publishes the epm binary so `epm.cmake` can download it. In parallel, the
eacp upstream PRs epm needs first: `Hash::sha256` (promoting the private SHA-1 alongside),
`Args::Parser` subcommands, `Processes::findExecutable` and `wait(timeout)`, string
`split`/`join` (or land eacp #177). epm pins eacp `develop` until these merge.

**Phase 1 — source parity with CPM + correctness (2–3 weeks)**
`epm_add_package` with the full CPM argument set and shorthand; request/response protocol;
git and URL fetchers; content-addressed source cache with completeness markers and locks;
patches hashed by content; tag/branch pinning; `epm.lock` read/write; prefetch; local
overrides (`EPM_`/`CPM_<Name>_SOURCE`, case-insensitive); CPM shim for transitive deps;
find_package redirect; OPTIONS/policies as CPM. Dogfood: EACPExamples, then eacp itself.
Exit criterion: a CPM project switches by renaming `CPMAddPackage` → `epm_add_package` and
configures byte-for-byte the same targets, faster on the second configure.

**Phase 2 — stores for sources (1–2 weeks)**
`Store` interface; Directory and HTTP backends; `epm cache push/pull/gc/verify`;
`EPM_OFFLINE`, `EPM_BLOCK_ORIGIN`, `EPM_FROZEN`; auth via env/credential helper. CI for a
dogfood project configures with no access to GitHub.

**Phase 3 — external prebuilt resources (1–2 weeks)**
`epm_add_prebuilt`, platform-key table, recipes with imported targets, multi-config
locations. Replace `FindWebView2.cmake`; model tamber-3rd-party's onnxruntime/whisper/tflite.

**Phase 4 — built-from-source binary cache (3–4 weeks)**
Toolchain probe, build-config-id, binary key, isolated child build with nested epm,
relocatable packaging, `find_package(CONFIG)` consumption, `TARGETS` assertion, `MODE
auto`, multi-config policy, `epm publish` from CI, GitHub Releases store. Requires
`install(EXPORT)` in eacp and friends (`epm_install_targets()` helper). Exit criterion: a
clean machine configures and links an eacp app with eacp coming from the binary cache, and
the same configure with `EPM_eacp_FROM_SOURCE=ON` produces identical target names.

**Phase 5 — polish and reach**
Dependency provider mode; signatures and trusted keys; `why`/`why-miss`; GC policies;
AgentWorkspace reads `epm.lock`; OCI/S3 stores; optional `.git`-keeping checkouts.

---

## 12. Decisions (confirmed 2026-11-02)

1. **No separate manifest file.** The CMakeLists is the manifest and `epm.lock` is the only
   extra file (unlike pkgcpp's `pkgcpp.json`). May be revisited later. *Revisited: the
   graph stage declares dependencies in an `epm.cpp` recipe (`Docs/Graph.md` §2).*
2. **Cached sources are clean snapshots without `.git`**, with `epm checkout` / workspace
   overrides for development (see 5.2).
3. **Artifact format `tar.zst`** via `cmake -E tar`; zip stays for vendor archives.
4. **Configs and ABI matching**: `EPM_CONFIGS auto` (single-config: your build type;
   multi-config: Debug+Release), configs additive per entry; `EPM_ABI_MATCH compatible`
   by default.
5. **Name matching case-insensitive** (diverges from CPM, fixes `cpmNames`).
6. **Generic additions go upstream into eacp** (SHA-256, `Args` subcommands, PATH lookup,
   `Processes::wait(timeout)`, string `split`/`join`). epm is the occasion to upgrade eacp;
   each addition ships as its own eacp PR with tests before epm depends on it.
7. **Separate `epm` repo**, depending on eacp like any other app.

Still open, with defaults chosen (no action needed unless you object):

- **Bootstrap order**: download release binary first, build-from-source fallback.
- **Full-mode dependency keys for static libraries** (strict; opt-in relaxation later).
