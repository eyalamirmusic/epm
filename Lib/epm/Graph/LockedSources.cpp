#include "LockedSources.h"

#include "ManifestLoader.h"

#include <exception>

namespace epm
{
namespace
{
constexpr auto graphRequester = "graph";

DependencyResolution::LockMode lockModeFor(const std::string& mode)
{
    if (mode == "update")
        return DependencyResolution::LockMode::update;

    if (mode == "offline")
        return DependencyResolution::LockMode::offline;

    return DependencyResolution::LockMode::locked;
}

DependencyResolution::RootOverride overrideFor(const std::string& reference)
{
    auto override = DependencyResolution::RootOverride {};

    if (isFullCommitHash(reference))
        override.commit = reference;
    else
        override.tag = reference;

    return override;
}
} // namespace

DependencyResolution::LockOptions lockOptionsFor(const LockSettings& settings,
                                                 const CacheRoot& cache,
                                                 const Env& env)
{
    auto options = DependencyResolution::LockOptions {};
    options.lockFile = eacp::FilePath {settings.lockFile};
    options.cacheRoot = cache.path();
    options.cmake = eacp::FilePath {env.cmake};
    options.mode = lockModeFor(settings.mode);
    options.isContinuousIntegration = settings.continuousIntegration;
    options.pinnedFiles = {manifestFileName};
    options.updateCommand = "cmake -B <build directory> -DEPM_LOCK_MODE=update";

    for (const auto& [name, reference]: settings.overrides)
        options.overrides[name] = overrideFor(reference);

    return options;
}

SourceFetcher makeLockedSourceFetcher(DependencyResolution::SourceResolver& resolver)
{
    return [&resolver](const std::string& name,
                       const std::string& url,
                       const std::string& ref)
    {
        auto result = SourceResult {};

        try
        {
            const auto resolved =
                resolver.resolve(name, {{name, graphRequester, url, ref, false}});
            resolver.verifyPinnedFile(
                name, resolved.sourceDirectory, manifestFileName);

            result.ok = true;
            result.snapshot.commit = resolved.commit;
            result.snapshot.sourceId = sourceId(url, resolved.commit);
            result.snapshot.directory = resolved.sourceDirectory;
        }
        catch (const std::exception& error)
        {
            result.error = error.what();
        }

        return result;
    };
}
} // namespace epm
