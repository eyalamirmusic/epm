#include "Package.h"

#include <epm/Build/ChildBuild.h>
#include <epm/Build/Configs.h>
#include <epm/Cache/BinaryCache.h>
#include <epm/Cache/PackageLock.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Text.h>

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
                               const Vector<std::string>& missing)
{
    return name + " is binary-only, but the cache has no " + wanted.platform
           + " entry holding " + joinList(configs, ", ") + " for " + shortId(wanted)
           + (existing ? " (entry " + existing->prefix.fileName() + " lacks "
                             + joinList(missing, ", ") + ")"
                       : "");
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
    return CacheRoot {FilePath {input.cacheRoot}};
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

SourceSnapshot Package::fetch()
{
    if (fetched)
        return *fetched;

    if (input.source)
    {
        fetched = snapshotOf(*input.source);
        return *fetched;
    }

    const auto& request = spec();

    // fetchSource takes the package lock, and eacp's IPC::Lock is not
    // re-entrant: this process would wait out the timeout on itself.
    if (holdsLock)
        throw BuildError {"the build step of " + request.name
                          + " fetches its source inside cached()'s callback, "
                            "under the package lock a fetch takes: call fetch() "
                            "before cached()"};

    const auto source =
        fetchSource(cache(), request.name, request.gitUrl, request.gitRef);

    if (!source.ok)
        throw BuildError {source.error};

    fetched = source.snapshot;
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

    auto existing = pick();
    auto missing = existing ? missingConfigs(existing->configs, configs) : configs;
    logEntry(request.name, wanted, existing, missing);

    switch (decide(request, existing && missing.empty()))
    {
        case Decision::useSource:
            return std::nullopt;

        case Decision::useBinary:
            return Prefix {*existing, wanted, {}};

        case Decision::missingBinary:
            throw BuildError {missingBinaryError(
                request.name, wanted, configs, existing, missing)};

        case Decision::buildBinary:
            break;
    }

    // Another process may have built the entry while this one waited.
    const auto lock = PackageLock {store, request.name, packageLockTimeout};

    if (!lock.isLocked())
        throw BuildError {"timed out waiting for another epm process using "
                          + request.name};

    existing = pick();

    if (!existing)
        existing = createEntry(store, request.name, wanted, wanted.sourceId);

    if (!existing)
        throw BuildError {"cannot create "
                          + entryPrefix(store, request.name, wanted).str()};

    const auto prefix = existing->prefix;
    missing = missingConfigs(existing->configs, configs);

    if (missing.empty())
        return Prefix {*existing, wanted, {}};

    for (const auto& config: missing)
        if (!clearConfig(prefix, config))
            throw BuildError {"cannot clear the leftovers of " + config + " in "
                              + prefix.str()};

    holdsLock = true;

    try
    {
        buildMissing(Prefix {*existing, wanted, missing});
    }
    catch (...)
    {
        holdsLock = false;
        throw;
    }

    holdsLock = false;

    for (const auto& config: missing)
    {
        const auto entry = readEntry(prefix);

        if (entry && entry->configs.contains(config))
            continue;

        if (!hasExport(prefix, config))
            throw BuildError {"the build step of " + request.name + " left no "
                              + exportFileFor(prefix, config).fileName()
                              + " (or its .names) in " + prefix.str()};

        if (auto error = addConfigToEntry(prefix, config, env()))
            throw BuildError {"adding " + config + " to the entry of " + request.name
                              + ": " + *error};
    }

    const auto built = readEntry(prefix);

    if (!built)
        throw BuildError {"cannot read the entry " + prefix.str()};

    return Prefix {*built, wanted, missing};
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
    base.dependencyGraph = dependencyGraph();
    base.epmExecutable = FilePath {input.epmExecutable};
    return CMakeBuild {std::move(base), store.build()};
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
    output.sourceDirectory = fetch().directory.str();
    output.prefix = entry.prefix.str();
    output.platform = entry.fingerprint.platform;
    output.configs = entry.configs;
    output.note = abiNote(entry.fingerprint, prefix.wanted()).value_or("");
    output.fingerprintHash = fingerprintId(entry.fingerprint);
    output.options = prefix.wanted().options;
}

void Package::useSource()
{
    output = BuildOutput {};
    output.name = spec().name;
    output.mode = "source";
    output.sourceDirectory = fetch().directory.str();
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
    const auto source = fetch();
    auto settings = cmake(source);

    if (configure)
        configure(settings);

    const auto buildMissing = [&](const Prefix& missing)
    {
        settings.into(missing).run();

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
    package.fetched.reset();
    package.output = BuildOutput {};
    package.output.name = input.request.name;
    package.holdsLock = false;
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
