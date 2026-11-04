# How epm picks one version of each package

epm resolves the dependency tree the way Go resolves modules: with minimal
version selection (Russ Cox, "Minimal Version Selection",
https://research.swtch.com/vgo-mvs, and https://go.dev/ref/mod#minimal-version-selection),
translated from Go's semantic-version-only world to git refs.

## Requirements

A requirement is what one project asks of a package: a URL and a ref, plus the
commit that ref names. Each package's requirements come from its own
`epm.json` `packages` (`Docs/Graph.md`) and its `epm_add_package`,
`CPMAddPackage` and `FetchContent_Declare` calls. The root project's
declarations are requirements like everyone else's, recorded as `root`; a name
the root's `epm.json` graph holds has that one definition, which a package call
for the same name loads rather than fetching another. Child builds of
prebuilt packages resolve their own calls against the root's lock, recorded
under the package's name.

A dependency that ships a lock (`epm.lock`, or a `lockstep.lock`, which has the
same form) lists the commits it was built with. Each entry is that dependency's
requirement on the package, as a go.mod is, and replaces what its CMake asks
for; it is never a second lock.

## Selection

The root `epm.lock` has exactly one entry per package name, so the whole tree
builds against one eacp. When the requirements on a package disagree:

- If every one is a semantic version tag (`v1.2.3`, `1.2`, `2`) of the same
  URL, the highest wins. That is Go's rule.
- If every one names the same commit of the same URL, by whatever ref, they
  agree.
- Otherwise (different commits, branches or URLs) the configure stops, naming
  each requester and its ref, and says to settle it in `epm.json`.

The lock records every requirement under its package, so a later configure
selects the same version from its first call. CMake adds a package the moment
it is asked for, so a higher tag that a later call asks for cannot replace one
already added: epm writes the higher one into the lock and asks for one more
configure, which then adds it from the start.

## epm.json

The `overrides` of `epm.json` beside the top-level `CMakeLists.txt` are go.mod's
`replace`, beside its `packages` and `profiles`:

```json
{ "packages": [ { "name": "eacp", "git": "eyalamirmusic/eacp", "tag": "develop" } ],
  "overrides": { "eacp": { "commit": "57b2ef60…" },
                 "Miro": { "url": "https://github.com/me/Miro.git", "tag": "v1.2" } } }
```

An override replaces every requirement on that package, and its lock entry
records `"isOverridden": true`. Only the root's `epm.json` counts; a
dependency's is never read, as Go ignores `replace` outside the main module.
Child builds of prebuilt packages read the root's. Both `epm.json` and the
lock are configure inputs, so editing either reconfigures the next build.

## The binary cache

A cache entry's fingerprint holds the package's Merkle hash, so a binary is
used only with exactly the sources in the lock. Each config of an entry also
records the hashes of the packages its child build took from the lock, and a
config whose dependencies the lock now holds at other hashes is rebuilt.
