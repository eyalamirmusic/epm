#include "Commands.h"
#include <epm/Emit/Response.h>
#include <epm/Resolve/Resolve.h>
#include <epm/Lock/LockFile.h>
#include <epm/Lock/Messages.h>
#include <epm/Spec/PackageArguments.h>
#include <epm/Source/PackageCache.h>
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
#include <epm/Cache/SourceCache.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Git.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Timestamp.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <stdexcept>

namespace epm
{
namespace
{
constexpr auto overrideSuffix = std::string_view {"_SOURCE"};
constexpr auto overridePrefixes = std::array<std::string_view, 2> {"EPM_", "CPM_"};

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

std::string getPathForLock(const eacp::FilePath& lockPath, const std::string& path)
{
    auto lockDirectory = eacp::toStdPath(lockPath).parent_path();
    auto relative =
        eacp::toStdPath(eacp::FilePath {path}).lexically_relative(lockDirectory);

    if (relative.empty())
        return path;

    return eacp::FilePath {relative}.str();
}

eacp::FilePath makeAbsolute(const eacp::FilePath& base, const std::string& path)
{
    auto asPath = eacp::FilePath {path};

    if (asPath.isAbsolute())
        return asPath;

    return eacp::FilePath {
        (eacp::toStdPath(base) / eacp::toStdPath(asPath)).lexically_normal()};
}

std::string getPathForUser(const CommandOptions& options, const eacp::FilePath& path)
{
    auto projectDirectory = eacp::toStdPath(options.sourceDirectory);

    if (projectDirectory.empty())
        projectDirectory = std::filesystem::current_path();

    auto relative = eacp::toStdPath(path).lexically_relative(projectDirectory);
    auto isInsideProject = !relative.empty() && *relative.begin() != "..";

    if (!isInsideProject)
        return path.str();

    return eacp::FilePath {relative}.str();
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

    return {getPathForUser(options, file),
            findPackageRequestLine(eacp::Files::readFile(file), name)};
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

LockedPackage resolvePackage(const PackageCache& cache,
                             const PackageRequest& request,
                             const std::vector<std::string>& patchesForLock)
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
                                           const PackageCache& cache,
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

PackageCache createPackageCache(const CommandOptions& options)
{
    return PackageCache {CacheRoot {options.cache}, options.cmake};
}

std::vector<std::string> getPatchesForLock(const CommandOptions& options,
                                           const PackageSpec& spec)
{
    auto patches = std::vector<std::string> {};

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

bool isLockUsed(const CommandOptions& options)
{
    return !options.lock.empty();
}

// Not in the lock, so the commit (or archive hash), the patches and the
// submodule selection are what name the tree.
std::string computeUnlockedSourceId(const PackageRequest& request,
                                    const std::string& commit)
{
    auto identity = commit + "\n" + Miro::toJSONString(request.submoduleSelection);

    for (const auto& patch: request.patches)
        identity += "\n" + computeFileSha256Hex(patch);

    return sourceId(request.url, identity);
}

std::string resolveRemoteCommit(const PackageRequest& request)
{
    if (request.kind == PackageKind::archive)
        return request.archiveHash.hex;

    if (isFullCommitHash(request.ref))
        return request.ref;

    const auto remote = Git::lsRemote(request.url);

    if (!remote.ok)
        throw std::runtime_error(
            describeFailedDownload({request.name, request.url, {}}) + "\n  "
            + remote.error);

    if (auto commit = resolveRef(remote.refs, request.ref))
        return *commit;

    throw std::runtime_error(request.url + " has no tag or branch named '"
                             + request.ref + "'");
}

// Without a lock the ref is resolved on every configure, as CPM does, and
// src/<sourceId>.json remembers which tree that commit unpacked to.
eacp::FilePath resolveUnlockedSource(const CommandOptions& options,
                                     const PackageSpec& spec)
{
    auto cache = createPackageCache(options);
    auto request = createRequest(options, spec);
    auto commit = resolveRemoteCommit(request);
    auto cacheRoot = CacheRoot {options.cache};
    auto marker =
        cacheRoot.src()
        / (computeUnlockedSourceId(request, commit).substr(0, 12) + ".json");

    if (!commit.empty())
    {
        if (auto known = loadJsonFile<SourceMarker>(marker))
        {
            if (cache.hasIntactEntry(known->hash, Verification::trustStamp))
                return cache.getSourceDirectory(known->hash);
        }
    }

    printStatus("fetching " + request.name + " at '" + request.ref + "' from "
                + request.url);
    auto gitCommit = request.kind == PackageKind::git ? commit : std::string {};
    auto materialized = cache.materialize(request, gitCommit, Network::allowed);

    if (!commit.empty())
        saveJsonFile(marker,
                     SourceMarker {request.url,
                                   materialized.commit,
                                   materialized.hash,
                                   utcTimestamp()});

    return materialized.sourceDirectory;
}

eacp::FilePath resolveLockedSource(const CommandOptions& options,
                                   const PackageSpec& spec,
                                   const Session& session)
{
    if (!isLockUsed(options))
        return resolveUnlockedSource(options, spec);

    auto cache = createPackageCache(options);
    auto lock = readLockFile(options.lock);
    auto request = createRequest(options, spec);

    if (options.mode == Mode::update || session.isWritingNewLock)
    {
        auto resolved =
            resolvePackage(cache, request, getPatchesForLock(options, spec));
        lock.storePackage(resolved);
        writeLockFile(options.lock, lock);
        return cache.getSourceDirectory(resolved.hash);
    }

    auto locked = lock.findPackage(spec.name);

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
    return ensureLockedPackageIsCached(
        options, cache, request, *locked, getNetworkForMode(options.mode));
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
};

ResolvedSource resolvePackageSource(const CommandOptions& options,
                                    const PackageSpec& spec,
                                    const Session& session)
{
    if (auto overridden = findOverride(spec, session))
    {
        auto setting = overridden->variable + "=" + overridden->sourceDirectory;

        if (options.isCI && isLockUsed(options) && options.mode == Mode::locked)
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
                {}};
    }

    if (spec.kind == PackageKind::local)
        return {makeAbsolute(options.requestedBy, spec.localSourceDirectory), {}};

    auto cached = resolveLockedSource(options, spec, session);

    if (spec.checkoutDirectory.empty())
        return {cached, cached.fileName()};

    return {placeCheckout(options, spec, cached), cached.fileName()};
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
        eacp::Vector<std::string> {name + "_SOURCE_DIR",
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
std::optional<eacp::FilePath>
    findOrBuildBinary(const CommandOptions& options,
                      const PackageSpec& spec,
                      const std::string& mode,
                      const eacp::FilePath& sourceDirectory,
                      const std::string& sourceHash)
{
    auto request = Request {};
    request.name = spec.name;
    request.gitUrl = spec.url;
    request.gitRef = spec.ref;
    request.mode = mode;
    request.buildMissingBinaries = options.buildMissingBinaries;

    for (const auto& [name, value]: spec.options)
        request.options[name] = value;

    auto source = SourceSnapshot {{}, sourceHash, sourceDirectory};
    auto build = LockForChildBuilds {options.lock, getChildLockMode(options)};
    auto result = resolveBinary(
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

    return result.response.prefix;
}

std::string addPackage(const CommandOptions& options, const PackageSpec& spec)
{
    auto session = readSession(options.buildDirectory);

    if (auto added = session.findPackage(spec.name))
    {
        if (added->requestedRef != spec.ref)
        {
            printStatus("WARNING: " + options.requestedBy.str() + " asks for "
                        + spec.name + " at '" + spec.ref
                        + "', but it was already added at '" + added->requestedRef
                        + "', which is kept");
        }

        return createPropagatingBlock(spec.name,
                                      createPackageVariables(*added, false));
    }

    auto source = resolvePackageSource(options, spec, session);
    auto package = SessionPackage {spec.name,
                                   spec.ref,
                                   source.directory.str(),
                                   getBinaryDirectory(options, spec.name),
                                   "source",
                                   source.hash};
    auto mode = spec.mode.empty() ? options.defaultMode : spec.mode;
    auto prefix = std::optional<eacp::FilePath> {};

    if (mode != "source" && !source.hash.empty() && !spec.isDownloadOnly)
        prefix =
            findOrBuildBinary(options, spec, mode, source.directory, source.hash);

    if (prefix)
    {
        package.mode = "prebuilt";
        package.binaryDirectory.clear();
    }

    session.packages.push_back(package);
    writeSession(options.buildDirectory, session);

    if (prefix)
        return createPropagatingBlock(spec.name,
                                      createPackageVariables(package, true)
                                          + prebuiltTargetsText(*prefix));

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
} // namespace

namespace
{
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
    if (isLockUsed(options) && options.mode != Mode::update
        && !isLockFileCurrent(options.lock))
    {
        printStatus(formatUserError(
            options.lock.str()
                + " is not in the form this epm writes: it is from another "
                  "version or was edited by hand. Rewrite it with:",
            "cmake -B <build> -DEPM_LOCK_MODE=update"));
    }

    auto session = Session {};
    session.isWritingNewLock = isLockUsed(options) && options.mode == Mode::locked
                               && !eacp::Files::exists(options.lock);

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

int runAddCommand(const CommandOptions& options)
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

int runUpdateCommand(const CommandOptions& options)
{
    auto cache = createPackageCache(options);
    auto lock = readLockFile(options.lock);
    auto updatedCount = 0;

    for (const auto& name: options.names)
    {
        if (!lock.findPackage(name))
        {
            auto lockedNames = eacp::Vector<std::string> {};

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
    auto cache = createPackageCache(options);
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

        printStatus(
            "FAILED " + package.name + ": "
            + describeDamagedCacheEntry(
                {package.name, cache.getSourceDirectory(package.hash).str()}));
        ++failedCount;
    }

    if (!isLockFileCurrent(options.lock))
    {
        printStatus(formatUserError(options.lock.str()
                                        + " is not in the form this lockstep "
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
