# DependencyResolution

epm's graph walk makes a plan: which packages the project needs, where each
comes from (a git URL) and which version each project asks for (a tag, a
branch or a commit). DependencyResolution makes that plan concrete. For every
package it picks one exact commit, records it in `epm-lock.json` beside the
project, and puts that commit's source tree on disk.

The promise: the same `epm-lock.json` gives the same source files on disk, on
every machine, every time. Nothing else. The binary cache, build directories
and fingerprints stay as they are in epm; only the source fetch goes through
this library. It knows nothing about recipes, the graph or CMake.

## The plan in, the sources out

```cpp
#include <DependencyResolution/DependencyResolution.h>

using namespace DependencyResolution;

auto plan = eacp::Vector<PlannedSource> {
    {"fmt", "root", "https://github.com/fmtlib/fmt.git", "11.0.2"},
    {"fmt", "spdlog", "https://github.com/fmtlib/fmt.git", "11.1.0"},
    {"Threads", "root", {}, {}, true}};

auto options = LockOptions {};
options.lockFile = projectDirectory / "epm-lock.json";
options.cacheRoot = cache.path();
options.mode = LockMode::locked;

auto sources = resolveSources(plan, options);

for (const auto& source: sources.packages)
    useSourceAt(source.name, source.sourceDirectory, source.commit, source.hash);
```

A `PlannedSource` is one project asking for one package: the package's name,
who asked, the URL and the ref. A package two projects ask for appears twice.
`isSystem` marks a package the machine provides (found by CMake's own search);
it is listed in `systemPackages` and never fetched or locked.

Each `ResolvedSource` has the package's source directory, the commit it was
checked out at, and a hash of every file in that tree (path, file type and
contents, `.git` left out).

epm's walk meets packages one at a time and reads each one's epm.cpp before
it knows the next, so it uses the same thing one package at a time:
`SourceResolver::resolve(name, plannedSources)`. `resolveSources` is a loop
over it.

## How a version is picked

When several projects ask for the same package:

- If every ask is a version tag (`v1.2.0`, `11.0.2`), the highest wins.
- If every ask names the same commit, that commit wins.
- Anything else stops with an error naming each project and what it asked
  for.

The root project settles it with an override (`LockOptions::overrides`): a
commit or tag for the package that replaces every ask. Only the root's
overrides count.

## What the lock records

For each package: its URL, the ref that was asked for, the commit it resolved
to, the hash of its files, who asked for what, whether an override chose it,
and the hash of any file the caller pins (epm pins `epm.cpp`). One more hash
covers all packages, so two locks compare by one line.

## The modes

- `locked` (the default): use what the lock records. A missing lock is
  written; a package the lock lacks is added to it. A project asking for
  something other than what the lock records stops with an error.
- `update`: ask the remotes again, pick versions again, rewrite the lock.
- `offline`: the lock and the cache only, never the network.
- Continuous integration (`isContinuousIntegration`, in `locked` mode): never
  writes the lock. A missing lock, or a package it lacks, is an error, so CI
  builds exactly what was committed.

When a package's tree is missing from the cache, it is fetched again at the
recorded commit and must hash to the recorded value, or the configure stops.

## Running a dependency's code

Before epm runs a dependency's `epm.cpp`, it calls
`SourceResolver::verifyPinnedFile(name, sourceDirectory, "epm.cpp")`. That
re-hashes the file and stops if it is not the one the lock recorded, so the
lock pins the code that runs and not only the files around it.
