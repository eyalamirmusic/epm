#include "Commands.h"
#include <epm/Emit/Response.h>
#include <epm/Emit/GraphResponse.h>
#include <epm/Graph/Graph.h>
#include <epm/Resolve/Resolve.h>
#include <epm/Spec/Overrides.h>
#include <epm/Lock/LockFile.h>
#include <epm/Lock/ErrorMessages.h>
#include <epm/Spec/PackageArguments.h>
#include <epm/Source/SourceCache.h>
#include <epm/Lock/Session.h>
#include <epm/Util/Status.h>

#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Format.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/Strings.h>

#include <ea_data_structures/Utilities/VectorUtilities.h>

#include <epm/Lock/MerkleHash.h>
#include <epm/Source/SourceSnapshot.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Spec/Manifest.h>
#include <eacp/Core/Utils/Sha256.h>
#include <eacp/Core/Utils/Time.h>

#include <algorithm>
#include <ea_data_structures/Structures/Array.h>
#include <filesystem>
#include <stdexcept>

namespace epm
{
namespace
{
constexpr auto jsonIndent = 2;
constexpr auto overrideSuffix = std::string_view {"_SOURCE"};
constexpr auto overridePrefixes = EA::Array<std::string_view, 2> {"EPM_", "CPM_"};

std::string getOverrideVariableName(std::string_view prefix, const std::string& name)
{
    return std::string {prefix} + name + std::string {overrideSuffix};
}

// An archive has no commit, so its content hash is what names the version.
std::string describeVersion(PackageKind kind,
                            const std::string& commit,
                            const std::string& hash)
{
    if (kind == PackageKind::archive)
        return "content " + hash;

    return commit + " (content " + hash + ")";
}

// The path with every symlink resolved, as far as it exists, so two spellings
// of one directory (macOS's /tmp and /private/tmp) compare equal.
eacp::FilePath resolveSymlinks(const eacp::FilePath& path)
{
    auto error = std::error_code {};
    auto resolved = std::filesystem::weakly_canonical(eacp::toStdPath(path), error);

    if (error)
        return path.lexicallyNormal();

    return eacp::FilePath {resolved};
}

std::string getPathForLock(const eacp::FilePath& lockPath, const std::string& path)
{
    auto relative = resolveSymlinks(eacp::FilePath {path})
                        .relativeTo(resolveSymlinks(lockPath.parentDirectory()));

    if (relative.empty())
        return path;

    return relative.str();
}

eacp::FilePath makeAbsolute(const eacp::FilePath& base, const std::string& path)
{
    auto asPath = eacp::FilePath {path};

    if (asPath.isAbsolute())
        return asPath;

    return (base / path).lexicallyNormal();
}

std::string getPathForUser(const CommandOptions& options, const eacp::FilePath& path)
{
    auto projectDirectory = options.sourceDirectory;

    if (projectDirectory.empty())
        projectDirectory = eacp::FilePath::currentDirectory();

    auto relative =
        resolveSymlinks(path).relativeTo(resolveSymlinks(projectDirectory)).str();
    auto isInsideProject =
        !relative.empty() && relative != ".." && !relative.starts_with("../");

    if (!isInsideProject)
        return path.str();

    return relative;
}

eacp::FilePath getRequestingFile(const CommandOptions& options)
{
    if (eacp::Files::isFile(options.requestedByFile))
        return options.requestedByFile;

    return options.requestedBy / "CMakeLists.txt";
}

RequestLocation getRequestLocation(const CommandOptions& options,
                                   const std::string& name)
{
    auto file = getRequestingFile(options);

    if (!eacp::Files::isFile(file))
        return {getPathForUser(options, options.requestedBy), std::nullopt};

    auto text = eacp::Files::readFile(file);
    auto line = options.isFindPackageRequest ? findFindPackageLine(text, name)
                                             : findPackageRequestLine(text, name);
    return {getPathForUser(options, file), line};
}

PackageRequest createRequestFromLock(const eacp::FilePath& lockPath,
                                     const LockedPackage& package)
{
    auto request = PackageRequest {
        package.name, package.kind, package.url, package.requestedRef, {}};
    request.submoduleSelection = package.submoduleSelection;

    for (const auto& patch: package.patches)
        request.patches.add(makeAbsolute(lockPath.parentDirectory(), patch));

    return request;
}

LockedPackage resolvePackage(const SourceCache& cache,
                             const PackageRequest& request,
                             const EA::Vector<std::string>& patchesForLock)
{
    printStatus("resolving " + request.name + " at '" + request.ref + "' from "
                + request.url);
    auto materialized = cache.materialize(request, {}, Network::allowed);
    printStatus(
        "locked " + request.name + " at "
        + describeVersion(request.kind, materialized.commit, materialized.hash));

    return {request.name,
            request.kind,
            request.url,
            request.ref,
            patchesForLock,
            request.submoduleSelection,
            materialized.commit,
            materialized.submodules,
            materialized.usesGitLfs,
            materialized.hash};
}

eacp::FilePath ensureLockedPackageIsCached(const CommandOptions& options,
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
                + ", which the lock does not record; re-lock it "
                  "with -DEPM_LOCK_MODE=update");
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
            {locked.name, locked.url, getPathForUser(options, options.lock)}));
    }

    return materialized.sourceDirectory;
}

Network getNetworkForMode(Mode mode)
{
    if (mode == Mode::offline)
        return Network::forbidden;

    return Network::allowed;
}

// CPM's rule: a relative patch is looked for beside the CMake file that names
// it, then under the directory of the CMakeLists.txt being processed.
eacp::FilePath findPatchFile(const CommandOptions& options, const std::string& patch)
{
    if (!options.requestedByFile.empty())
    {
        auto besideFile =
            makeAbsolute(options.requestedByFile.parentDirectory(), patch);

        if (eacp::Files::isFile(besideFile))
            return besideFile;
    }

    return makeAbsolute(options.requestedBy, patch);
}

PackageRequest createRequest(const CommandOptions& options, const PackageSpec& spec)
{
    auto request = PackageRequest {spec.name, spec.kind, spec.url, spec.ref, {}};
    request.submoduleSelection = spec.submoduleSelection;
    request.archiveHash = spec.archiveHash;

    for (const auto& patch: spec.patches)
        request.patches.add(findPatchFile(options, patch));

    return request;
}

SourceCache createSourceCache(const CommandOptions& options)
{
    return SourceCache {CacheRoot {options.cache}, options.cmake};
}

EA::Vector<std::string> getPatchesForLock(const CommandOptions& options,
                                          const PackageSpec& spec)
{
    auto patches = EA::Vector<std::string> {};

    for (const auto& patch: spec.patches)
    {
        auto absolute = findPatchFile(options, patch);
        patches.push_back(getPathForLock(options.lock, absolute.str()));
    }

    return patches;
}

std::string describeRequest(const std::string& url,
                            const std::string& ref,
                            const std::string& otherUrl)
{
    if (url != otherUrl)
        return eacp::Strings::format("from {} at '{}'", url, ref);

    return eacp::Strings::format("at '{}'", ref);
}

void checkRequestMatchesLock(const CommandOptions& options,
                             const PackageSpec& spec,
                             const LockedPackage& locked)
{
    auto isSameSource = locked.url == spec.url && locked.requestedRef == spec.ref
                        && locked.kind == spec.kind;
    auto isSamePatches = locked.patches == getPatchesForLock(options, spec);
    auto isSameSubmodules =
        isSameSelection(locked.submoduleSelection, spec.submoduleSelection);

    if (isSameSource && isSamePatches && isSameSubmodules)
        return;

    auto mismatch =
        LockMismatch {getRequestLocation(options, spec.name),
                      spec.name,
                      getPathForUser(options, options.lock),
                      describeRequest(spec.url, spec.ref, locked.url),
                      describeRequest(locked.url, locked.requestedRef, spec.url),
                      getPathForUser(options, options.buildDirectory)};

    if (isSameSource && !isSamePatches)
    {
        mismatch.requestedDescription = "with other patches";
        mismatch.lockedDescription = "with the patches it records";
    }
    else if (isSameSource)
    {
        mismatch.requestedDescription = "with other submodules";
        mismatch.lockedDescription = "with the submodules it records";
    }

    throw std::runtime_error(describeLockMismatch(mismatch));
}

// A tree in the cache and the commit it was checked out at, empty for an
// archive.
struct CachedSource
{
    eacp::FilePath directory;
    std::string commit;
};

CachedSource resolveLockedSource(const CommandOptions& options,
                                 const PackageSpec& spec,
                                 const Session& session)
{
    auto cache = createSourceCache(options);
    auto lock = readLockFile(options.lock);
    auto request = createRequest(options, spec);

    if (options.mode == Mode::update || session.isWritingNewLock)
    {
        auto resolved =
            resolvePackage(cache, request, getPatchesForLock(options, spec));
        lock.storePackage(resolved);
        writeLockFile(options.lock, lock);
        return {cache.getSourceDirectory(resolved.hash), resolved.commit};
    }

    auto locked = lock.findPackage(spec.name);
    auto isWritingMissingEntry = !locked && options.isFindPackageRequest
                                 && options.mode == Mode::locked && !options.isCI;

    if (isWritingMissingEntry)
    {
        auto resolved =
            resolvePackage(cache, request, getPatchesForLock(options, spec));
        lock.storePackage(resolved);
        writeLockFile(options.lock, lock);
        printStatus("added " + spec.name + " to "
                    + getPathForUser(options, options.lock) + "; commit it");
        return {cache.getSourceDirectory(resolved.hash), resolved.commit};
    }

    if (!locked)
    {
        throw std::runtime_error(describeUnlockedRequest(
            {getRequestLocation(options, spec.name),
             spec.name,
             getPathForUser(options, options.lock),
             eacp::Files::isFile(options.lock),
             getPathForUser(options, options.buildDirectory)}));
    }

    checkRequestMatchesLock(options, spec, *locked);
    return {ensureLockedPackageIsCached(
                options, cache, request, *locked, getNetworkForMode(options.mode)),
            locked->commit};
}

std::optional<SessionOverride> findOverride(const PackageSpec& spec,
                                            const Session& session)
{
    if (auto overridden = session.findOverride(spec.name))
        return overridden;

    for (auto prefix: overridePrefixes)
    {
        auto variable = getOverrideVariableName(prefix, spec.name);

        if (auto value = eacp::getEnvValue(variable); !value.empty())
            return SessionOverride {spec.name, value, variable};
    }

    return std::nullopt;
}

std::string getCheckoutStamp(const eacp::FilePath& checkout, const std::string& hash)
{
    return hash + " " + computeTreeStamp(checkout);
}

// SOURCE_DIR beside a repository or URL: the locked tree is copied there, and
// copied again whenever the stamp kept in the build directory stops matching.
eacp::FilePath placeCheckout(const CommandOptions& options,
                             const PackageSpec& spec,
                             const eacp::FilePath& cached)
{
    auto checkout =
        makeAbsolute(options.requestedBinaryDirectory, spec.checkoutDirectory);
    auto stampPath =
        options.buildDirectory / "epm" / "checkouts" / (spec.name + ".stamp");
    auto hash = cached.fileName();

    if (eacp::Files::isFile(stampPath) && eacp::Files::isDirectory(checkout)
        && eacp::Files::readFile(stampPath) == getCheckoutStamp(checkout, hash))
        return checkout;

    printStatus("checking out " + spec.name + " into " + checkout.str());
    eacp::Files::removeAll(checkout);

    if (!eacp::Files::copyDirectory(cached, checkout))
        throw std::runtime_error(formatUserError(
            "epm could not copy " + spec.name + " into its SOURCE_DIR "
                + checkout.str() + ".",
            "Make " + checkout.str() + " writable, then configure again."));

    eacp::Files::createDirectories(stampPath.parentDirectory());
    eacp::Files::writeFile(stampPath,
                           std::string_view {getCheckoutStamp(checkout, hash)});
    return checkout;
}

// The tree a package's CMake is added from, and the Merkle hash of the
// cached tree behind it; no hash for a local checkout or an override.
struct ResolvedSource
{
    eacp::FilePath directory;
    std::string hash;
    std::string commit;
};

ResolvedSource resolvePackageSource(const CommandOptions& options,
                                    const PackageSpec& spec,
                                    const Session& session)
{
    if (auto overridden = findOverride(spec, session))
    {
        auto setting = overridden->variable + "=" + overridden->sourceDirectory;

        if (options.isCI && options.mode == Mode::locked)
        {
            throw std::runtime_error(
                formatUserError(setting + " overrides " + spec.name
                                    + ", which EPM_CI forbids in locked mode.",
                                "Unset " + overridden->variable
                                    + ", or configure without -DEPM_CI=ON."));
        }

        printStatus("WARNING: " + spec.name + " comes from the local override "
                    + setting + ", not from the lock");
        return {makeAbsolute(options.sourceDirectory, overridden->sourceDirectory),
                {},
                {}};
    }

    if (spec.kind == PackageKind::local)
        return {
            makeAbsolute(options.requestedBy, spec.localSourceDirectory), {}, {}};

    auto cached = resolveLockedSource(options, spec, session);

    auto hash = cached.directory.fileName();

    if (spec.checkoutDirectory.empty())
        return {cached.directory, hash, cached.commit};

    return {placeCheckout(options, spec, cached.directory), hash, cached.commit};
}

std::string getBinaryDirectory(const CommandOptions& options,
                               const std::string& name)
{
    auto lowerName = eacp::Strings::toLower(name);
    return (options.buildDirectory / "_deps" / (lowerName + "-build")).str();
}

std::string createVariable(const std::string& name, const std::string& value)
{
    return "set(" + name + " " + cmakeQuoted(value) + ")\n";
}

std::string createPackageVariables(const SessionPackage& package, bool isAdded)
{
    auto snippet = std::string {};
    snippet += createVariable(package.name + "_SOURCE_DIR", package.sourceDirectory);
    snippet += createVariable(package.name + "_BINARY_DIR", package.binaryDirectory);

    if (isAdded)
        snippet += createVariable(package.name + "_ADDED", "YES");
    else
        snippet += createVariable(package.name + "_ADDED", "NO");

    snippet += createVariable(package.name + "_MODE", package.mode);
    snippet += createVariable("CPM_LAST_PACKAGE_NAME", package.name);
    snippet += createVariable("CPM_PACKAGE_" + package.name + "_SOURCE_DIR",
                              package.sourceDirectory);
    snippet += createVariable("CPM_PACKAGE_" + package.name + "_BINARY_DIR",
                              package.binaryDirectory);
    return snippet;
}

std::string createAddSubdirectory(const PackageSpec& spec,
                                  const SessionPackage& package)
{
    auto subdirectory = eacp::FilePath {package.sourceDirectory};

    if (!spec.sourceSubdirectory.empty())
        subdirectory = subdirectory / spec.sourceSubdirectory;

    if (spec.isDownloadOnly || !eacp::Files::isFile(subdirectory / "CMakeLists.txt"))
        return {};

    auto snippet = std::string {};

    for (const auto& [name, value]: spec.options)
        snippet += createVariable(name, value);

    snippet += "set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)\n";
    snippet += "add_subdirectory(" + cmakeQuoted(subdirectory.str()) + " "
               + cmakeQuoted(package.binaryDirectory);

    if (spec.isExcludedFromAll)
        snippet += " EXCLUDE_FROM_ALL";

    if (spec.isSystem)
        snippet += " SYSTEM";

    return snippet + ")\n";
}

// What CPM writes after adding a package, so a later find_package(<Name>) in
// config mode finds the package already added instead of an installed one.
std::string createFindPackageRedirect(const PackageSpec& spec)
{
    if (spec.isDownloadOnly)
        return {};

    auto lowerName = eacp::Strings::toLower(spec.name);
    auto redirects =
        std::string {"${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/"} + lowerName;
    return "file(WRITE \"" + redirects
           + "-config.cmake\" \"include(\\\"\\${CMAKE_CURRENT_LIST_DIR}/" + lowerName
           + "-extra.cmake\\\" OPTIONAL)\\n\")\n" + "file(WRITE \"" + redirects
           + "-config-version.cmake\" \"set(PACKAGE_VERSION_COMPATIBLE TRUE)\\n"
             "set(PACKAGE_VERSION_EXACT TRUE)\\n\")\n";
}

// Every command that includes the snippet is a function, so the package's
// variables are handed on to its caller, as CPM's cpm_export_variables does.
std::string createPropagatingBlock(const std::string& name, const std::string& body)
{
    auto propagated =
        EA::Vector<std::string> {name + "_SOURCE_DIR",
                                 name + "_BINARY_DIR",
                                 name + "_ADDED",
                                 name + "_MODE",
                                 "CPM_LAST_PACKAGE_NAME",
                                 "CPM_PACKAGE_" + name + "_SOURCE_DIR",
                                 "CPM_PACKAGE_" + name + "_BINARY_DIR"};
    auto snippet = "block(SCOPE_FOR VARIABLES PROPAGATE "
                   + eacp::Strings::join(propagated, " ") + ")\n" + body
                   + "endblock()\n";

    for (const auto& variable: propagated)
        snippet += "set(" + variable + " \"${" + variable + "}\" PARENT_SCOPE)\n";

    return snippet;
}

// A child build adds the packages its dependency asks for to the same lock.
// It runs in update mode whenever this configure writes the lock, since the
// child's packages are not in it yet.
std::string getChildLockMode(const CommandOptions& options)
{
    if (options.mode == Mode::update
        || readSession(options.buildDirectory).isWritingNewLock)
        return "update";

    if (options.mode == Mode::offline)
        return "offline";

    return "locked";
}

// epm's binary cache: the request it keys on, then MODE's choice between the
// cached libraries and add_subdirectory. nullopt means source.
std::optional<Response> findOrBuildBinary(const CommandOptions& options,
                                          const PackageSpec& spec,
                                          const std::string& mode,
                                          const ResolvedSource& resolved)
{
    auto request = Request {};
    request.name = spec.name;
    request.gitUrl = spec.url;
    request.gitRef = spec.ref;
    request.mode = mode;
    request.buildMissingBinaries = options.buildMissingBinaries;
    request.configs = options.configs;
    request.abiMatch = options.abiMatch;

    for (const auto& [name, value]: spec.options)
        request.options[name] = value;

    auto source =
        SourceSnapshot {resolved.commit, resolved.hash, resolved.directory};
    auto build = LockForChildBuilds {
        options.lock, getChildLockMode(options), options.manifest};
    if (auto error = requestError(request))
        throw std::runtime_error(*error);

    auto result = resolveSnapshot(
        request, options.env, CacheRoot {options.cache}, source, build);

    if (!result.ok)
    {
        auto message = result.error;

        if (!result.details.empty())
            message = result.details + "\n" + message;

        throw std::runtime_error(message);
    }

    if (result.response.mode != "prebuilt")
        return std::nullopt;

    return result.response;
}

bool isResolvingRequirements(const CommandOptions& options, const Session& session)
{
    return options.mode == Mode::update || session.isWritingNewLock;
}

bool isUnderDirectory(const std::string& path, const std::string& directory)
{
    return path.starts_with(directory)
           && (path.size() == directory.size() || path[directory.size()] == '/');
}

// The package whose CMake made this call: the added package whose tree holds
// the requesting directory, else the top-level project.
std::string identifyRequester(const CommandOptions& options, const Session& session)
{
    auto requesting = resolveSymlinks(options.requestedBy).str();
    auto requester = options.rootName;
    auto longestMatch = std::size_t {0};

    for (const auto& package: session.packages)
    {
        auto directory =
            resolveSymlinks(eacp::FilePath {package.sourceDirectory}).str();

        if (!package.sourceDirectory.empty() && directory.size() > longestMatch
            && isUnderDirectory(requesting, directory))
        {
            requester = package.name;
            longestMatch = directory.size();
        }
    }

    return requester;
}

// epm.json's override, as Go's replace: its URL and its commit or tag stand
// in for whatever the call asked for.
std::optional<Requirement> findOverrideRequirement(const CommandOptions& options,
                                                   const PackageSpec& spec)
{
    auto override = readRootManifest(options.manifest).findOverride(spec.name);

    if (!override)
        return std::nullopt;

    auto requirement = Requirement {"epm.json", spec.url, spec.ref, {}};

    if (!override->url.empty())
        requirement.url = override->url;

    if (!override->commit.empty())
        requirement.requestedRef = override->commit;
    else if (!override->tag.empty())
        requirement.requestedRef = override->tag;

    return requirement;
}

PackageSpec withRequirement(PackageSpec spec, const Requirement& requirement)
{
    spec.url = requirement.url;
    spec.ref = requirement.requestedRef;
    return spec;
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

// What the requirement's ref names: from the lock in locked and offline
// mode, which then must record it, and from upstream while resolving.
std::string resolveRequirementCommit(const CommandOptions& options,
                                     const PackageSpec& spec,
                                     const Requirement& requirement,
                                     const Session& session)
{
    if (!requirement.commit.empty())
        return requirement.commit;

    if (isResolvingRequirements(options, session))
    {
        auto request = createRequest(options, withRequirement(spec, requirement));
        return createSourceCache(options).resolveCommit(request, Network::allowed);
    }

    auto locked = readLockFile(options.lock).findPackage(spec.name);

    if (!locked)
    {
        throw std::runtime_error(describeUnlockedRequest(
            {getRequestLocation(options, spec.name),
             spec.name,
             getPathForUser(options, options.lock),
             eacp::Files::isFile(options.lock),
             getPathForUser(options, options.buildDirectory)}));
    }

    if (auto commit = findLockedCommit(*locked, requirement))
        return *commit;

    throw std::runtime_error(describeLockMismatch(
        {getRequestLocation(options, spec.name),
         spec.name,
         getPathForUser(options, options.lock),
         describeRequest(requirement.url, requirement.requestedRef, locked->url)
             + " for " + requirement.requestedBy,
         "with the requirements it records",
         getPathForUser(options, options.buildDirectory)}));
}

void recordRequirementsInLock(const CommandOptions& options,
                              const std::string& name,
                              const EA::Vector<Requirement>& requirements,
                              bool isOverridden)
{
    auto lock = readLockFile(options.lock);
    auto locked = lock.findPackage(name);

    if (!locked)
        return;

    locked->requirements = requirements;
    locked->isOverridden = isOverridden;
    std::sort(locked->requirements.begin(),
              locked->requirements.end(),
              [](const Requirement& left, const Requirement& right)
              {
                  return std::tie(left.requestedBy, left.url, left.requestedRef)
                         < std::tie(
                             right.requestedBy, right.url, right.requestedRef);
              });
    lock.storePackage(*locked);
    writeLockFile(options.lock, lock);
}

// A dependency that ships an epm-lock.json lists what it was built with; each
// entry is its requirement, as a go.mod is, never a second lock.
void addShippedRequirements(Session& session,
                            const std::string& name,
                            const eacp::FilePath& sourceDirectory)
{
    auto shipped = sourceDirectory / lockFileName;

    if (!eacp::Files::isFile(shipped))
        return;

    for (const auto& package: readLockFile(shipped).packages)
        session.addRequirement(package.name,
                               {name, package.url, package.commit, package.commit});
}

void replaceRequirementOf(EA::Vector<Requirement>& requirements,
                          const Requirement& requirement)
{
    for (auto& existing: requirements)
    {
        if (existing.requestedBy == requirement.requestedBy)
        {
            existing = requirement;
            return;
        }
    }

    requirements.add(requirement);
}

// Every requirement on `name` the selection runs over, one per requester:
// those the lock recorded when it was last resolved, so a configure selects
// from the start what it selected then, then this configure's, then this one.
EA::Vector<Requirement> gatherRequirements(const CommandOptions& options,
                                           const Session& session,
                                           const std::string& name,
                                           const Requirement& requirement)
{
    auto requirements = EA::Vector<Requirement> {};

    if (auto locked = readLockFile(options.lock).findPackage(name);
        locked && !locked->isOverridden)
    {
        for (const auto& recorded: locked->requirements)
            replaceRequirementOf(requirements, recorded);
    }

    for (const auto& known: session.findRequirements(name))
        replaceRequirementOf(requirements, known);

    replaceRequirementOf(requirements, requirement);
    return requirements;
}

std::string describeRaisedRequirement(const std::string& name,
                                      const SessionPackage& added,
                                      const Requirement& selected)
{
    return formatUserError(
        name + " was added at '" + added.requestedRef + "', and then "
            + selected.requestedBy + " asked for '" + selected.requestedRef
            + "', which minimal version selection takes instead. The lock now "
              "holds '"
            + selected.requestedRef + "'.",
        "Configure again; " + name + " is then added at '" + selected.requestedRef
            + "' from the start.");
}

// A package call for a name the epm.json graph holds: the graph's definition
// is loaded through the provider, as find_package would.
std::string createGraphPackageBlock(const std::string& name)
{
    auto quoted = cmakeQuoted(name);
    auto body =
        "epm_provider_load(" + quoted + ")\n"
        + "get_property(_epm_mode GLOBAL PROPERTY EPM_PACKAGE_" + name + "_MODE)\n"
        + "get_property(_epm_source GLOBAL PROPERTY EPM_PACKAGE_" + name
        + "_SOURCE_DIR)\n" + "get_property(_epm_binary GLOBAL PROPERTY EPM_PACKAGE_"
        + name + "_BINARY_DIR)\n" + "set(" + name
        + "_SOURCE_DIR \"${_epm_source}\")\n" + "set(" + name
        + "_BINARY_DIR \"${_epm_binary}\")\n" + "set(" + name
        + "_MODE \"${_epm_mode}\")\n" + "set(" + name + "_ADDED YES)\n"
        + "set(CPM_LAST_PACKAGE_NAME " + quoted + ")\n";

    return createPropagatingBlock(name, body);
}

std::string addPackage(const CommandOptions& options, const PackageSpec& requested)
{
    if (options.graphPackages.contains(requested.name))
        return createGraphPackageBlock(requested.name);

    auto session = readSession(options.buildDirectory);
    auto requester = identifyRequester(options, session);
    auto override = findOverrideRequirement(options, requested);
    auto requirement = override.value_or(
        session.findRequirementBy(requested.name, requester)
            .value_or(Requirement {requester, requested.url, requested.ref, {}}));
    auto added = session.findPackage(requested.name);

    // A local checkout, given by SOURCE_DIR alone or by an <Name>_SOURCE
    // override, is outside the lock and so outside selection.
    auto isLocal =
        requested.kind == PackageKind::local || findOverride(requested, session);

    if (added && (override || isLocal))
        return createPropagatingBlock(requested.name,
                                      createPackageVariables(*added, false));

    auto selection = RequirementSelection {false, requirement};

    if (!isLocal)
    {
        requirement.commit =
            resolveRequirementCommit(options, requested, requirement, session);
        auto requirements = EA::Vector<Requirement> {requirement};

        if (!override)
            requirements =
                gatherRequirements(options, session, requested.name, requirement);

        selection = selectRequirement(requirements);

        if (selection.isConflict)
            throw std::runtime_error(
                describeRequirementConflict(requested.name, requirements));

        session.addRequirement(requested.name, requirement);
    }

    auto spec = isLocal ? requested : withRequirement(requested, selection.selected);
    auto isOverridden = override.has_value();

    if (added)
    {
        writeSession(options.buildDirectory, session);

        if (selection.selected.commit != added->commit)
        {
            if (isResolvingRequirements(options, session))
            {
                resolveLockedSource(options, spec, session);
                recordRequirementsInLock(
                    options, spec.name, session.findRequirements(spec.name), false);
            }

            throw std::runtime_error(
                describeRaisedRequirement(spec.name, *added, selection.selected));
        }

        if (isResolvingRequirements(options, session))
            recordRequirementsInLock(
                options, spec.name, session.findRequirements(spec.name), false);

        return createPropagatingBlock(spec.name,
                                      createPackageVariables(*added, false));
    }

    auto source = resolvePackageSource(options, spec, session);

    if (!isLocal && isResolvingRequirements(options, session))
        recordRequirementsInLock(
            options, spec.name, session.findRequirements(spec.name), isOverridden);

    auto package = SessionPackage {spec.name,
                                   spec.ref,
                                   source.directory.str(),
                                   getBinaryDirectory(options, spec.name),
                                   "source",
                                   source.hash,
                                   selection.selected.commit};
    auto mode = spec.mode.empty() ? options.defaultMode : spec.mode;
    auto prebuilt = std::optional<Response> {};

    if (mode != "source" && !source.hash.empty() && !spec.isDownloadOnly)
        prebuilt = findOrBuildBinary(options, spec, mode, source);

    if (prebuilt)
    {
        package.mode = "prebuilt";
        package.binaryDirectory.clear();
    }

    session.packages.push_back(package);

    if (!prebuilt && !source.hash.empty())
        addShippedRequirements(session, spec.name, source.directory);

    writeSession(options.buildDirectory, session);

    if (prebuilt)
        return createPropagatingBlock(spec.name,
                                      createPackageVariables(package, true)
                                          + prebuiltLoadText(*prebuilt));

    return createPropagatingBlock(spec.name,
                                  createPackageVariables(package, true)
                                      + createAddSubdirectory(spec, package)
                                      + createFindPackageRedirect(spec));
}

void writeSnippet(const CommandOptions& options, const std::string& snippet)
{
    eacp::Files::createDirectories(options.output.parentDirectory());
    eacp::Files::writeFile(options.output, std::string_view {snippet});
}

SessionOverride parseOverride(const std::string& definition)
{
    auto equals = definition.find('=');
    auto variable = definition.substr(0, equals);

    for (auto prefix: overridePrefixes)
    {
        if (equals == std::string::npos
            || variable.size() <= prefix.size() + overrideSuffix.size()
            || !variable.starts_with(prefix) || !variable.ends_with(overrideSuffix))
            continue;

        auto nameLength = variable.size() - prefix.size() - overrideSuffix.size();
        return SessionOverride {variable.substr(prefix.size(), nameLength),
                                definition.substr(equals + 1),
                                variable};
    }

    throw std::runtime_error("--override takes EPM_<Name>_SOURCE=<dir> or "
                             "CPM_<Name>_SOURCE=<dir>, not "
                             + definition);
}
// One package of an epm.json graph through the lock: each declaration is a
// requirement, the root's epm.json overrides replace them all, minimal version
// selection chooses, and the choice is locked and fetched like any other.
LockedSource lockGraphPackage(const CommandOptions& options,
                              Session& session,
                              const std::string& name,
                              const EA::Vector<Requirement>& declared)
{
    auto requested = PackageSpec {};
    requested.name = name;
    requested.kind = PackageKind::git;
    requested.url = declared.front().url;
    requested.ref = declared.front().requestedRef;

    auto override = findOverrideRequirement(options, requested);
    auto requirements = EA::Vector<Requirement> {};

    if (override)
    {
        override->commit =
            resolveRequirementCommit(options, requested, *override, session);
        requirements.add(*override);
    }
    else
    {
        for (auto requirement: declared)
        {
            requirement.commit =
                resolveRequirementCommit(options, requested, requirement, session);
            session.addRequirement(name, requirement);
            requirements = gatherRequirements(options, session, name, requirement);
        }
    }

    auto selection = selectRequirement(requirements);

    if (selection.isConflict)
        return {false, {}, {}, describeRequirementConflict(name, requirements)};

    auto spec = withRequirement(requested, selection.selected);
    auto source = resolvePackageSource(options, spec, session);

    if (isResolvingRequirements(options, session))
        recordRequirementsInLock(options,
                                 name,
                                 override ? requirements
                                          : session.findRequirements(name),
                                 override.has_value());

    // Recorded, so the package calls its own CMake makes are attributed to it.
    if (!session.findPackage(name))
        session.packages.push_back({name,
                                    selection.selected.requestedRef,
                                    source.directory.str(),
                                    {},
                                    "graph",
                                    source.hash,
                                    selection.selected.commit});

    return {
        true,
        SourceSnapshot {selection.selected.commit, source.hash, source.directory},
        selection.selected,
        {}};
}
} // namespace

namespace
{
bool isSystemPackage(const CommandOptions& options, const std::string& name)
{
    auto isNamed = [&](const ManifestPackage& package)
    { return package.system && package.name == name; };

    auto manifest = readRootManifest(options.manifest);

    if (EA::Vectors::findIf(manifest.packages, isNamed))
        return true;

    return false;
}

std::optional<Requirement> findKnownSource(const CommandOptions& options,
                                           const std::string& name)
{
    if (auto locked = readLockFile(options.lock).findPackage(name);
        locked && locked->kind != PackageKind::local)
        return Requirement {{}, locked->url, locked->requestedRef, locked->commit};

    auto known = readSession(options.buildDirectory).findRequirements(name);

    if (known.empty())
        return std::nullopt;

    return known.front();
}

std::string describeUnknownFindPackage(const CommandOptions& options,
                                       const std::string& name)
{
    auto location = getRequestLocation(options, name);
    return formatUserError(
        formatRequestLocation(location) + ": find_package(" + name
            + ") names a package that is not in the graph, and no epm.json or "
              "epm-lock.json in it says where "
            + name + " comes from.",
        "Add it to the packages in epm.json, as\n    { \"name\": \"" + name
            + "\", \"git\": \"<owner/repo>\", \"tag\": \"<tag>\" }\n  or, for "
              "one CMake's own search should find on this machine,\n    { \"name\": \""
            + name + "\", \"system\": true }");
}

PackageSpec parseArgumentsFrom(const CommandOptions& options)
{
    try
    {
        return parsePackageArguments(options.packageArguments);
    }
    catch (const std::invalid_argument& error)
    {
        throw std::runtime_error(options.requestedBy.str() + ": " + error.what());
    }
}
} // namespace

int runBeginCommand(const CommandOptions& options)
{
    if (options.mode != Mode::update && !isLockFileCurrent(options.lock))
    {
        printStatus(formatUserError(
            options.lock.str()
                + " is not in the form this epm writes: it is from another "
                  "version or was edited by hand. Rewrite it with:",
            "cmake -B <build> -DEPM_LOCK_MODE=update"));
    }

    if (options.isCI && options.mode == Mode::locked
        && !eacp::Files::exists(options.lock))
    {
        throw std::runtime_error(formatUserError(
            options.lock.str() + " does not exist, and EPM_CI forbids writing it.",
            "Configure once without -DEPM_CI=ON and commit the lock it writes."));
    }

    auto session = Session {};
    session.isWritingNewLock =
        options.mode == Mode::locked && !eacp::Files::exists(options.lock);

    for (const auto& definition: options.overrides)
    {
        auto entry = parseOverride(definition);

        if (!entry.sourceDirectory.empty())
            session.overrides.push_back(entry);
    }

    std::stable_partition(
        session.overrides.begin(),
        session.overrides.end(),
        [](const auto& entry)
        { return entry.variable.starts_with(overridePrefixes[0]); });

    writeSession(options.buildDirectory, session);
    return 0;
}

int runConfigureCommand(const CommandOptions& options)
{
    auto inputs = GraphInputs {};
    inputs.environment = eacp::getEnv;
    inputs.manifestDirectory = options.manifest.parentDirectory();
    inputs.env = options.env;

    auto loaded = loadManifest(options.manifest);

    if (!loaded.ok)
        throw std::runtime_error("the manifest " + options.manifest.str() + ": "
                                 + loaded.error);

    inputs.manifest = std::move(loaded.manifest);

    if (auto overrides = loadJsonFile<Overrides>(options.overridesFile))
        inputs.overrides = std::move(*overrides);
    else
        throw std::runtime_error("cannot read the overrides file "
                                 + options.overridesFile.str());

    auto session = readSession(options.buildDirectory);
    auto fetch =
        [&](const std::string& name, const EA::Vector<Requirement>& declared)
    {
        try
        {
            return lockGraphPackage(options, session, name, declared);
        }
        catch (const std::exception& error)
        {
            return LockedSource {false, {}, {}, error.what()};
        }
    };
    auto childLock = LockForChildBuilds {
        options.lock, getChildLockMode(options), options.manifest};
    auto result =
        configureGraph(inputs, CacheRoot {options.cache}, fetch, childLock);

    writeSession(options.buildDirectory, session);

    if (!result.ok)
    {
        auto message = result.error;

        if (!result.details.empty())
            message = result.details + "\n" + message;

        throw std::runtime_error(message);
    }

    auto graphJson = options.outputDirectory / "graph.json";
    auto graphCMake = options.outputDirectory / "graph.cmake";

    eacp::Files::writeFileAtomically(
        graphJson, Miro::toJSONString(result.graph, jsonIndent) + "\n");
    eacp::Files::writeFileAtomically(graphCMake, graphCMakeText(result.graph));

    return 0;
}

int runResolveCommand(const CommandOptions& options)
{
    auto spec = parseArgumentsFrom(options);
    writeSnippet(options, addPackage(options, spec));
    return 0;
}

int runProvideCommand(const CommandOptions& options)
{
    auto spec = parseArgumentsFrom(options);

    if (spec.kind == PackageKind::local)
    {
        writeSnippet(options, {});
        return 0;
    }

    auto snippet = addPackage(options, spec);
    snippet += "FetchContent_SetPopulated(" + spec.name + " SOURCE_DIR \"${"
               + spec.name + "_SOURCE_DIR}\" BINARY_DIR \"${" + spec.name
               + "_BINARY_DIR}\")\n";
    writeSnippet(options, snippet);
    return 0;
}

int runFindCommand(const CommandOptions& options)
{
    auto findOptions = options;
    findOptions.isFindPackageRequest = true;
    if (options.packageArguments.size() != 2
        || options.packageArguments[0] != "NAME")
        throw std::runtime_error("epm find takes -- NAME <name>");

    auto name = options.packageArguments[1];

    if (isSystemPackage(findOptions, name))
    {
        writeSnippet(findOptions, "set(_epm_find_is_system TRUE)\n");
        return 0;
    }
    auto source = findKnownSource(findOptions, name);

    if (!source)
        throw std::runtime_error(describeUnknownFindPackage(findOptions, name));
    findOptions.packageArguments = {"NAME",
                                    name,
                                    "GIT_REPOSITORY",
                                    source->url,
                                    "GIT_TAG",
                                    source->requestedRef};

    writeSnippet(findOptions,
                 addPackage(findOptions, parseArgumentsFrom(findOptions)));
    return 0;
}

int runUpdateCommand(const CommandOptions& options)
{
    auto cache = createSourceCache(options);
    auto lock = readLockFile(options.lock);
    auto updatedCount = 0;

    for (const auto& name: options.names)
    {
        if (!lock.findPackage(name))
        {
            auto lockedNames = EA::Vector<std::string> {};

            for (const auto& package: lock.packages)
                lockedNames.add(package.name);

            throw std::runtime_error(describeUnknownUpdateName(
                {name, getPathForUser(options, options.lock), lockedNames}));
        }
    }

    for (const auto& package: lock.packages)
    {
        if (!options.names.empty() && !options.names.contains(package.name))
            continue;

        auto request = createRequestFromLock(options.lock, package);
        lock.storePackage(resolvePackage(cache, request, package.patches));
        ++updatedCount;
    }

    writeLockFile(options.lock, lock);
    printStatus("updated " + std::to_string(updatedCount) + " package(s); root "
                + readLockFile(options.lock).root);
    return 0;
}

int runVerifyCommand(const CommandOptions& options)
{
    auto cache = createSourceCache(options);
    auto lock = readLockFile(options.lock);
    auto failedCount = 0;

    for (const auto& package: lock.packages)
    {
        auto verification =
            options.isFullVerify ? Verification::rehash : Verification::trustStamp;

        if (cache.hasIntactEntry(package.hash, verification))
        {
            printStatus("ok " + package.name + " " + package.hash);
            continue;
        }

        // Not in the cache, as a package only another platform asks for is
        // not: fetch it at its locked commit and check it hashes as locked.
        if (!eacp::Files::isDirectory(cache.getSourceDirectory(package.hash)))
        {
            try
            {
                ensureLockedPackageIsCached(
                    options,
                    cache,
                    createRequestFromLock(options.lock, package),
                    package,
                    getNetworkForMode(options.mode));
                printStatus("ok " + package.name + " " + package.hash
                            + " (fetched)");
            }
            catch (const std::exception& error)
            {
                printStatus("FAILED " + package.name + ": " + error.what());
                ++failedCount;
            }

            continue;
        }

        printStatus(
            "FAILED " + package.name + ": "
            + describeDamagedCacheEntry(
                {package.name, cache.getSourceDirectory(package.hash).str()}));
        ++failedCount;
    }

    if (!isLockFileCurrent(options.lock))
    {
        printStatus(formatUserError(options.lock.str()
                                        + " is not in the form this epm "
                                          "writes. Rewrite it with:",
                                    "epm lock update"));
        ++failedCount;
    }

    if (lock.root != lock.computeRootHash())
    {
        printStatus(formatUserError("the root hash in " + options.lock.str()
                                        + " does not match its packages, so it was "
                                          "edited by hand. Rewrite it with:",
                                    "epm lock update"));
        ++failedCount;
    }

    if (failedCount > 0)
        return 1;

    return 0;
}

int runRootCommand(const CommandOptions& options)
{
    if (!options.shouldAnnounce)
    {
        eacp::Console::print(readLockFile(options.lock).computeRootHash());
        return 0;
    }

    if (!eacp::Files::isFile(options.lock))
        return 0;

    auto lock = readLockFile(options.lock);

    if (readSession(options.buildDirectory).isWritingNewLock)
    {
        eacp::Console::print(
            "-- epm: no " + options.lock.fileName() + ", so wrote one with "
            + std::to_string(lock.packages.size()) + " packages; commit it.");
    }

    eacp::Console::print("-- epm: root " + lock.computeRootHash());

    return 0;
}

} // namespace epm
