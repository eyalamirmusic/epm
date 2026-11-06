#include "Package.h"

#include <epm/Build/ChildBuild.h>
#include <epm/Build/Configs.h>
#include <epm/Build/Harvest.h>
#include <epm/Cache/BinaryCache.h>
#include <epm/Cache/CacheStore.h>
#include <epm/Cache/PackageLock.h>
#include <epm/Emit/Response.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>

#include <exception>

namespace epm
{
using namespace eacp;

namespace
{
std::string shortId(const AbiFingerprint& fingerprint)
{
    return fingerprintId(fingerprint).substr(0, 12);
}

void logEntry(const std::string& name,
              const AbiFingerprint& wanted,
              const std::optional<BinaryEntry>& existing,
              const Vector<std::string>& missing)
{
    Log::step("epm: " + name + " " + wanted.platform + " " + shortId(wanted) + ": "
              + (existing ? "entry " + existing->prefix.fileName() : "no entry")
              + (missing.empty() ? "" : ", missing " + joinList(missing, ", ")));
}

std::string missingBinaryError(const std::string& name,
                               const AbiFingerprint& wanted,
                               const Vector<std::string>& configs,
                               const std::optional<BinaryEntry>& existing,
                               const Vector<std::string>& missing,
                               std::size_t stores)
{
    const auto where =
        stores == 0
            ? std::string {"the cache has"}
            : "neither the cache nor its " + std::to_string(stores)
                  + (stores == 1 ? " cache store has" : " cache stores have");

    return name + " is binary-only, but " + where + " no " + wanted.platform
           + " entry holding " + joinList(configs, ", ") + " for " + shortId(wanted)
           + (existing ? " (entry " + existing->prefix.fileName() + " lacks "
                             + joinList(missing, ", ") + ")"
                       : "");
}

// set(epm_<name>_SOURCE_DIR ...) for each of `sources`, fetched now by the
// commit epm resolved: appended to the graph a child build loads, so a
// package that names a prebuilt dependency's <name>_SOURCE_DIR (its include/,
// say) finds the snapshot. Each fetch takes that dependency's lock, never the
// one the building step holds.
std::string dependencySourceLines(const CacheRoot& cache,
                                  const Vector<DependencySource>& sources)
{
    auto lines = std::string {};

    for (const auto& source: sources)
    {
        const auto fetched = fetchCommit(
            cache, source.name, source.gitUrl, source.gitRef, source.commit);

        if (!fetched.ok)
            throw BuildError {"fetching the source of the dependency " + source.name
                              + ": " + fetched.error};

        lines += "set(epm_" + source.name + "_SOURCE_DIR "
                 + cmakeQuoted(fetched.snapshot.directory.str()) + ")\n";
    }

    return lines;
}

BuildOutput failedOutput(const std::string& name,
                         std::string error,
                         std::string details = {})
{
    auto output = BuildOutput {};
    output.name = name;
    output.error = std::move(error);
    output.details = std::move(details);
    return output;
}
} // namespace

void Package::build()
{
    buildWithCMake({}, {});
}

const Request& Package::spec() const
{
    return input.request;
}

const Env& Package::env() const
{
    return input.env;
}

CacheRoot Package::cache() const
{
    auto stores = Vector<FilePath> {};

    for (const auto& store: input.cacheStores)
        stores.add(FilePath {store});

    return CacheRoot {FilePath {input.cacheRoot}, std::move(stores)};
}

const Vector<PackageDependency>& Package::dependencies() const
{
    return input.dependencies;
}

const std::string& Package::dependencyGraph() const
{
    return input.dependencyGraph;
}

bool Package::verbose() const
{
    return input.verbose;
}

SourceSnapshot Package::resolveSource()
{
    if (fetched)
        return *fetched;

    if (resolved)
        return *resolved;

    if (input.source)
    {
        resolved = snapshotOf(*input.source);
        return *resolved;
    }

    const auto& request = spec();
    const auto source =
        epm::resolveSource(cache(), request.name, request.gitUrl, request.gitRef);

    if (!source.ok)
        throw BuildError {source.error};

    resolved = source.snapshot;
    return *resolved;
}

SourceSnapshot Package::fetch()
{
    if (!holdsLock)
        sourceRequested = true;

    return fetchSnapshot();
}

SourceSnapshot Package::fetchSnapshot()
{
    if (fetched)
        return *fetched;

    const auto source = resolveSource();

    if (!source.directory.empty())
    {
        fetched = source;
        return *fetched;
    }

    // The commit epm resolved, so the snapshot is the one the fingerprint
    // names even if the ref has moved since. Under cached()'s callback this
    // process holds the package lock already, and eacp's IPC::Lock is not
    // re-entrant.
    const auto& request = spec();
    const auto snapshot =
        source.commit.empty()
            ? fetchSource(
                  cache(), request.name, request.gitUrl, request.gitRef, holdsLock)
            : fetchCommit(cache(),
                          request.name,
                          request.gitUrl,
                          request.gitRef,
                          source.commit,
                          holdsLock);

    if (!snapshot.ok)
        throw BuildError {snapshot.error};

    fetched = snapshot.snapshot;
    return *fetched;
}

AbiFingerprint Package::fingerprint(const SourceSnapshot& source) const
{
    return makeFingerprint(spec(), env(), source.sourceId);
}

AbiFingerprint Package::fingerprint(const SourceSnapshot& source,
                                    const CMakeBuild& build) const
{
    auto request = spec();
    request.options = build.definedValues();
    return makeFingerprint(request, env(), source.sourceId);
}

std::optional<Prefix>
    Package::cached(const AbiFingerprint& wanted,
                    const std::function<void(const Prefix&)>& buildMissing)
{
    const auto& request = spec();

    if (request.mode == "source")
        return std::nullopt;

    const auto store = cache();
    const auto policy =
        parseAbiMatch(request.abiMatch).value_or(AbiMatch::compatible);
    const auto configs = buildConfigsFor(request.configs);
    const auto pick = [&]
    {
        return pickEntry(findEntries(store, request.name, wanted.platform),
                         wanted,
                         policy,
                         configs);
    };

    const auto missingOf = [&](const std::optional<BinaryEntry>& entry)
    {
        return entry ? missingConfigs(entry->usableConfigs(wanted), configs)
                     : configs;
    };

    auto existing = pick();
    auto missing = missingOf(existing);
    logEntry(request.name, wanted, existing, missing);

    if (!missing.empty() && !store.stores().empty())
    {
        const auto pulled =
            pullFromStores(store, request.name, wanted, policy, configs);

        for (const auto& note: pulled.notes)
            Log::progress("epm: note: " + note);

        for (const auto& copied: pulled.copied)
            Log::progress("epm: " + request.name + ": copied " + copied
                          + " into the cache");

        if (!pulled.copied.empty())
        {
            existing = pick();
            missing = missingOf(existing);
            logEntry(request.name, wanted, existing, missing);
        }
    }

    switch (decide(request, existing && missing.empty()))
    {
        case Decision::useSource:
            return std::nullopt;

        case Decision::useBinary:
            return Prefix {*existing, wanted, {}};

        case Decision::missingBinary:
            throw BuildError {missingBinaryError(request.name,
                                                 wanted,
                                                 configs,
                                                 existing,
                                                 missing,
                                                 store.stores().size())};

        case Decision::buildBinary:
            break;
    }

    // Only to keep two local processes from building the same thing twice:
    // publishing is atomic, so the entry is valid without it (BinaryCache.h).
    // Another process may have built the entry while this one waited.
    const auto lock = PackageLock {store, request.name, packageLockTimeout};

    if (!lock.isLocked())
        throw BuildError {"timed out waiting for another epm process using "
                          + request.name};

    existing = pick();

    if (!existing)
        existing = exactEntry(wanted);

    missing = missingConfigs(existing->usableConfigs(wanted), configs);

    if (missing.empty())
        return Prefix {*existing, wanted, {}};

    auto entry = buildAndPublish(*existing, wanted, missing, buildMissing);
    auto built = missing;
    auto still = missingConfigs(entry.usableConfigs(wanted), configs);

    // The configs went to the exact entry (LTO objects of a compiler the entry
    // picked was not recorded for), which may lack the others; or another
    // process published one this compiler cannot link first: the exact entry
    // takes them all.
    if (!still.empty())
    {
        if (entry.prefix != entryPrefix(store, request.name, wanted))
        {
            entry = exactEntry(wanted);
            still = missingConfigs(entry.usableConfigs(wanted), configs);
        }

        if (!still.empty())
        {
            built.addFrom(still);
            entry = buildAndPublish(entry, wanted, still, buildMissing);
            still = missingConfigs(entry.usableConfigs(wanted), configs);
        }
    }

    removeStagingIfEmpty(entry.prefix);

    if (!still.empty())
        throw BuildError {
            "the entry " + entry.prefix.str() + " of " + request.name + " holds "
            + joinList(still, ", ") + " as LTO objects of another compiler, which "
            + wanted.compilerId + " " + wanted.compilerVersion + " cannot link"};

    return Prefix {entry, wanted, canonicalConfigs(built)};
}

BinaryEntry Package::exactEntry(const AbiFingerprint& wanted) const
{
    const auto store = cache();
    const auto& name = spec().name;

    if (auto entry = createEntry(store, name, wanted, wanted.sourceId))
        return *entry;

    throw BuildError {"cannot create " + entryPrefix(store, name, wanted).str()};
}

BinaryEntry
    Package::buildAndPublish(const BinaryEntry& entry,
                             const AbiFingerprint& wanted,
                             const Vector<std::string>& configs,
                             const std::function<void(const Prefix&)>& buildMissing)
{
    const auto& name = spec().name;
    removeStaleStaging(stagingDirectory(entry.prefix));
    const auto staging = newStaging(entry.prefix);

    if (!Files::createDirectories(staging.path()))
        throw BuildError {"cannot create " + staging.path().str()};

    touchStagingHeartbeat(staging.path());
    holdsLock = true;

    try
    {
        buildMissing(Prefix {entry, wanted, configs, staging.path()});
    }
    catch (...)
    {
        holdsLock = false;
        throw;
    }

    holdsLock = false;
    auto markers = Vector<ConfigMarker> {};

    for (const auto& config: configs)
    {
        const auto unit = configDirectory(staging.path(), config);

        if (!hasExport(unit, config))
            throw BuildError {"the build step of " + name + " left no "
                              + exportFileFor(unit, config).fileName()
                              + " (or its .names) in " + unit.str()};

        auto staged = describeStagedConfig(staging.path(), config, env());

        if (!staged.marker)
            throw BuildError {"publishing " + config + " of " + name + ": "
                              + staged.error};

        markers.add(std::move(*staged.marker));
    }

    // LTO objects link only with the compiler that wrote them: they go only
    // into an entry recorded for that compiler, this one's exact entry, so a
    // compatible entry never holds a config its own compiler cannot link.
    const auto pinned = markers.findIf(
        [&](const ConfigMarker& marker)
        {
            return !marker.lto.empty()
                   && (marker.compilerId != entry.fingerprint.compilerId
                       || marker.compilerVersion
                              != entry.fingerprint.compilerVersion);
        });

    const auto target = pinned ? exactEntry(wanted) : entry;

    if (pinned)
        Log::step("epm: " + name + " " + pinned->config + " holds " + pinned->lto
                  + " objects: publishing into the exact entry "
                  + target.prefix.fileName());

    for (const auto& marker: markers)
    {
        const auto published = publishConfig(target.prefix, staging.path(), marker);

        if (published.result == PublishResult::failed)
            throw BuildError {"publishing " + marker.config + " of " + name + ": "
                              + published.error};

        if (published.result == PublishResult::alreadyThere)
            Log::step("epm: " + name + " " + marker.config
                      + " was published by another process meanwhile");
    }

    if (auto published = readEntry(target.prefix))
        return *published;

    throw BuildError {"cannot read the entry " + target.prefix.str()};
}

CMakeBuild Package::cmake(const SourceSnapshot& source) const
{
    const auto store = cache();

    auto base = ChildBuild {};
    base.request = spec();
    base.env = env();
    base.generator = chooseGenerator(env(), findExecutable("ninja"));
    base.sourceDirectory = source.directory;
    base.cpmCache = store.cpm();
    base.cacheRoot = store.path();
    base.dependencyGraph = dependencyGraph();
    base.epmExecutable = FilePath {input.epmExecutable};

    auto build = CMakeBuild {std::move(base), store.build()};
    build.dependencySources = [store, sources = input.dependencySources]
    { return dependencySourceLines(store, sources); };
    return build;
}

CMakeBuild Package::cmake(const SourceSnapshot& source, const Prefix& prefix) const
{
    auto build = cmake(source);
    build.into(prefix);
    return build;
}

void Package::exportPrefix(const Prefix& prefix)
{
    const auto& entry = prefix.entry();

    output = BuildOutput {};
    output.name = spec().name;
    output.mode = "prebuilt";

    // Only a checkout the step asked for: one the default steps fetched to
    // build would give a SOURCE_DIR to the configure that built and none to
    // the next.
    if (sourceRequested && fetched)
        output.sourceDirectory = fetched->directory.str();
    output.prefix = entry.prefix.str();
    output.platform = entry.fingerprint.platform;

    // What the facade loads: a config of LTO objects this compiler cannot
    // link, which a compatible entry picked for others may hold, never is.
    output.configs = entry.usableConfigs(prefix.wanted());
    output.note = abiNote(entry.fingerprint, prefix.wanted()).value_or("");
    output.fingerprintHash = fingerprintId(entry.fingerprint);
    output.compatibleKey = abiKey(entry.fingerprint, AbiMatch::compatible);
    output.anyKey = abiKey(entry.fingerprint, AbiMatch::any);
    output.options = prefix.wanted().options;
}

void Package::useSource()
{
    output = BuildOutput {};
    output.name = spec().name;
    output.mode = "source";
    output.sourceDirectory = fetchSnapshot().directory.str();
}

void Package::useSource(const CMakeBuild& build)
{
    useSource();
    output.options = build.definedValues();
}

void Package::log(std::string_view line) const
{
    Log::progress("epm: " + spec().name + ": " + std::string {line});
}

void Package::buildWithCMake(const std::function<void(CMakeBuild&)>& configure,
                             const std::function<void(const Prefix&)>& afterBuild)
{
    const auto source = resolveSource();
    auto settings = cmake(source);

    if (configure)
        configure(settings);

    const auto buildMissing = [&](const Prefix& missing)
    {
        settings.source(fetchSnapshot().directory).into(missing).run();

        if (afterBuild)
            afterBuild(missing);
    };

    if (const auto prefix = cached(fingerprint(source, settings), buildMissing))
        exportPrefix(*prefix);
    else
        useSource(settings);
}

void PackageRunner::setInput(Package& package, const BuildInput& input)
{
    package.input = input;
    package.resolved.reset();
    package.fetched.reset();
    package.output = BuildOutput {};
    package.output.name = input.request.name;
    package.holdsLock = false;
    package.sourceRequested = false;
}

BuildOutput runPackage(Package& package, const BuildInput& input)
{
    const auto& name = input.request.name;
    PackageRunner::setInput(package, input);

    try
    {
        package.build();
    }
    catch (const BuildError& error)
    {
        return failedOutput(name, error.what(), error.details);
    }
    catch (const std::exception& error)
    {
        return failedOutput(name,
                            "the build step of " + name + " threw: " + error.what());
    }
    catch (...)
    {
        return failedOutput(name, "the build step of " + name + " threw");
    }

    if (package.output.mode.empty())
        return failedOutput(name,
                            "the build step of " + name
                                + " recorded no result: end build() with "
                                  "exportPrefix() or useSource()");

    return package.output;
}
} // namespace epm
