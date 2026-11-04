#include <DependencyResolution/DependencyResolution.h>

#include <DependencyResolution/Lock/ErrorMessages.h>
#include <DependencyResolution/Lock/Requirements.h>
#include <DependencyResolution/Source/SourceCache.h>
#include <DependencyResolution/Util/Status.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/Sha256.h>

#include <ea_data_structures/Utilities/VectorUtilities.h>

#include <algorithm>
#include <stdexcept>
#include <tuple>

namespace DependencyResolution
{
namespace
{
constexpr auto rootOverrideRequester = "root override";

std::string describeVersion(PackageKind kind,
                            const std::string& commit,
                            const std::string& hash)
{
    if (kind == PackageKind::archive)
        return "content " + hash;

    return commit + " (content " + hash + ")";
}

std::string describeRequest(const std::string& url,
                            const std::string& ref,
                            const std::string& otherUrl)
{
    if (url != otherUrl)
        return eacp::Strings::format("from {} at '{}'", url, ref);

    return eacp::Strings::format("at '{}'", ref);
}

Network getNetworkForMode(LockMode mode)
{
    if (mode == LockMode::offline)
        return Network::forbidden;

    return Network::allowed;
}

SourceCache createSourceCache(const LockOptions& options)
{
    return SourceCache {CacheRoot {options.cacheRoot}, options.cmake};
}

PackageRequest createRequest(const std::string& name, const Requirement& requirement)
{
    auto request = PackageRequest {};
    request.name = name;
    request.kind = PackageKind::git;
    request.url = requirement.url;
    request.ref = requirement.requestedRef;
    return request;
}

std::string hashFileIn(const eacp::FilePath& sourceDirectory,
                       const std::string& relativePath)
{
    auto file = sourceDirectory / relativePath;

    if (!eacp::Files::isFile(file))
        return {};

    return eacp::computeFileSha256Hex(file);
}

LockedPackage resolvePackage(const SourceCache& cache,
                             const PackageRequest& request,
                             const EA::Vector<std::string>& pinnedFiles)
{
    printStatus("resolving " + request.name + " at '" + request.ref + "' from "
                + request.url);
    auto materialized = cache.materialize(request, {}, Network::allowed);
    printStatus(
        "locked " + request.name + " at "
        + describeVersion(request.kind, materialized.commit, materialized.hash));

    auto locked = LockedPackage {request.name,
                                 request.kind,
                                 request.url,
                                 request.ref,
                                 {},
                                 request.submoduleSelection,
                                 materialized.commit,
                                 materialized.submodules,
                                 materialized.usesGitLfs,
                                 materialized.hash};

    for (const auto& file: pinnedFiles)
    {
        auto hash = hashFileIn(materialized.sourceDirectory, file);

        if (!hash.empty())
            locked.pinnedFiles[file] = hash;
    }

    return locked;
}

eacp::FilePath ensureLockedPackageIsCached(const LockOptions& options,
                                           const SourceCache& cache,
                                           const PackageRequest& request,
                                           const LockedPackage& locked,
                                           Network network)
{
    if (cache.hasIntactEntry(locked.hash, Verification::trustStamp))
        return cache.getSourceDirectory(locked.hash);

    cache.removeEntry(locked.hash);
    printStatus("fetching " + locked.name + " "
                + describeVersion(locked.kind, locked.commit, locked.hash)
                + " into the cache");
    auto materialized = cache.materialize(request, locked.commit, network);

    for (const auto& submodule: materialized.submodules)
    {
        auto isLockedEntry = [&](const LockedSubmodule& entry)
        { return isSameSubmodule(entry, submodule); };

        if (!EA::Vectors::findIf(locked.submodules, isLockedEntry))
        {
            throw std::runtime_error(
                locked.name + "'s submodule " + submodule.path + " is at "
                + submodule.commit + " from " + submodule.url
                + ", which the lock does not record; re-lock it: "
                + options.updateCommand);
        }
    }

    if (materialized.submodules.size() != locked.submodules.size())
    {
        throw std::runtime_error(locked.name + " checks out "
                                 + std::to_string(materialized.submodules.size())
                                 + " submodules, but the lock records "
                                 + std::to_string(locked.submodules.size()));
    }

    if (materialized.hash != locked.hash)
    {
        throw std::runtime_error(describeChangedContent(
            {locked.name, locked.url, options.lockFile.str()}));
    }

    return materialized.sourceDirectory;
}

std::optional<std::string> findLockedCommit(const LockedPackage& locked,
                                            const Requirement& requirement)
{
    for (const auto& recorded: locked.requirements)
    {
        if (isSameRequirement(recorded, requirement))
            return recorded.commit;
    }

    return std::nullopt;
}

UnlockedRequest describeMissingEntry(const LockOptions& options,
                                     const std::string& name,
                                     const std::string& requestedBy)
{
    return {{requestedBy, std::nullopt},
            name,
            options.lockFile.str(),
            eacp::Files::isFile(options.lockFile),
            options.updateCommand};
}

std::optional<Requirement> findOverrideRequirement(const LockOptions& options,
                                                   const std::string& name,
                                                   const PlannedSource& first)
{
    auto found = options.overrides.find(name);

    if (found == options.overrides.end())
        return std::nullopt;

    const auto& override = found->second;
    auto requirement = Requirement {rootOverrideRequester, first.url, first.ref, {}};

    if (!override.url.empty())
        requirement.url = override.url;

    if (!override.commit.empty())
        requirement.requestedRef = override.commit;
    else if (!override.tag.empty())
        requirement.requestedRef = override.tag;

    return requirement;
}

void sortRequirements(EA::Vector<Requirement>& requirements)
{
    std::sort(requirements.begin(),
              requirements.end(),
              [](const Requirement& left, const Requirement& right)
              {
                  return std::tie(left.requestedBy, left.url, left.requestedRef)
                         < std::tie(
                             right.requestedBy, right.url, right.requestedRef);
              });
}
} // namespace

SourceResolver::SourceResolver(LockOptions optionsToUse)
    : options(std::move(optionsToUse))
{
    auto lockExists = eacp::Files::exists(options.lockFile);

    if (options.mode != LockMode::update && lockExists
        && !isLockFileCurrent(options.lockFile))
    {
        printStatus(formatUserError(
            options.lockFile.str()
                + " is not in the form this epm writes: it is from another "
                  "version or was edited by hand. Rewrite it with:",
            options.updateCommand));
    }

    if (options.isContinuousIntegration && options.mode == LockMode::locked
        && !lockExists)
    {
        throw std::runtime_error(formatUserError(
            options.lockFile.str()
                + " does not exist, and continuous integration forbids writing it.",
            "Resolve once outside continuous integration and commit the lock it "
            "writes."));
    }

    isWritingNewLock = options.mode == LockMode::locked && !lockExists;
}

ResolvedSource SourceResolver::resolve(const std::string& name,
                                       const EA::Vector<PlannedSource>& requests)
{
    if (requests.empty())
        throw std::runtime_error("no request for " + name);

    auto cache = createSourceCache(options);
    auto lock = readLockFile(options.lockFile);
    auto locked = lock.findPackage(name);
    auto isResolvingUpstream = options.mode == LockMode::update || isWritingNewLock;
    auto isAddingMissingEntry = !locked && options.mode == LockMode::locked
                                && !options.isContinuousIntegration;

    if (!isResolvingUpstream && !locked && !isAddingMissingEntry)
    {
        throw std::runtime_error(describeUnlockedRequest(
            describeMissingEntry(options, name, requests.front().requestedBy)));
    }

    auto resolveCommit = [&](const Requirement& requirement)
    {
        if (isResolvingUpstream || isAddingMissingEntry)
        {
            return cache.resolveCommit(createRequest(name, requirement),
                                       Network::allowed);
        }

        if (auto commit = findLockedCommit(*locked, requirement))
            return *commit;

        throw std::runtime_error(describeLockMismatch(
            {{requirement.requestedBy, std::nullopt},
             name,
             options.lockFile.str(),
             describeRequest(requirement.url, requirement.requestedRef, locked->url)
                 + " for " + requirement.requestedBy,
             "with the requirements it records",
             options.updateCommand}));
    };

    auto requirements = EA::Vector<Requirement> {};
    auto override = findOverrideRequirement(options, name, requests.front());

    if (override)
    {
        override->commit = resolveCommit(*override);
        requirements.add(*override);
    }
    else
    {
        for (const auto& request: requests)
        {
            auto requirement =
                Requirement {request.requestedBy, request.url, request.ref, {}};
            requirement.commit = resolveCommit(requirement);
            requirements.add(requirement);
        }
    }

    auto selection = selectRequirement(requirements);

    if (selection.isConflict)
        throw std::runtime_error(describeRequirementConflict(name, requirements));

    auto request = createRequest(name, selection.selected);
    auto resolved = ResolvedSource {name, {}, {}, {}};

    if (isResolvingUpstream || isAddingMissingEntry)
    {
        auto entry = resolvePackage(cache, request, options.pinnedFiles);
        entry.requirements = requirements;
        entry.isOverridden = override.has_value();
        sortRequirements(entry.requirements);
        lock.storePackage(entry);
        writeLockFile(options.lockFile, lock);

        if (isAddingMissingEntry)
            printStatus("added " + name + " to " + options.lockFile.str()
                        + "; commit it");

        return {
            name, entry.commit, entry.hash, cache.getSourceDirectory(entry.hash)};
    }

    if (locked->url != request.url || locked->requestedRef != request.ref)
    {
        throw std::runtime_error(describeLockMismatch(
            {{requests.front().requestedBy, std::nullopt},
             name,
             options.lockFile.str(),
             describeRequest(request.url, request.ref, locked->url),
             describeRequest(locked->url, locked->requestedRef, request.url),
             options.updateCommand}));
    }

    auto directory = ensureLockedPackageIsCached(
        options, cache, request, *locked, getNetworkForMode(options.mode));
    return {name, locked->commit, locked->hash, directory};
}

void SourceResolver::verifyPinnedFile(const std::string& name,
                                      const eacp::FilePath& sourceDirectory,
                                      const std::string& relativePath) const
{
    auto locked = readLockFile(options.lockFile).findPackage(name);

    if (!locked)
        return;

    auto recorded = std::string {};

    if (auto found = locked->pinnedFiles.find(relativePath);
        found != locked->pinnedFiles.end())
        recorded = found->second;

    if (hashFileIn(sourceDirectory, relativePath) == recorded)
        return;

    throw std::runtime_error(
        formatUserError(name + "'s " + relativePath + " in " + sourceDirectory.str()
                            + " is not the one " + options.lockFile.str()
                            + " records, so it will not run.",
                        "Delete that directory to fetch it again, or re-lock: "
                            + options.updateCommand));
}

std::string SourceResolver::getRootHash() const
{
    return readLockFile(options.lockFile).computeRootHash();
}

ResolvedSources resolveSources(const EA::Vector<PlannedSource>& requests,
                               const LockOptions& options)
{
    auto resolver = SourceResolver {options};
    auto order = EA::Vector<std::string> {};
    auto byName = std::map<std::string, EA::Vector<PlannedSource>> {};
    auto resolution = ResolvedSources {};

    for (const auto& request: requests)
    {
        if (request.isSystem)
        {
            if (!resolution.systemPackages.contains(request.name))
                resolution.systemPackages.add(request.name);

            continue;
        }

        if (!byName.contains(request.name))
            order.add(request.name);

        byName[request.name].add(request);
    }

    for (const auto& name: order)
        resolution.packages.add(resolver.resolve(name, byName[name]));

    resolution.rootHash = resolver.getRootHash();
    return resolution;
}
} // namespace DependencyResolution
