#include "Resolve.h"

#include <epm/Build/Configs.h>
#include <epm/Recipe/CMakePackage.h>
#include <epm/Util/Log.h>

#include <eacp/Core/Utils/Files.h>

namespace epm
{
namespace
{
ResolveResult failure(std::string error, std::string details = {})
{
    auto result = ResolveResult {};
    result.error = std::move(error);
    result.details = std::move(details);
    return result;
}
} // namespace

ResolveResult resolveResultFor(const BuildOutput& output)
{
    if (!output.ok())
        return failure(output.error, output.details);

    auto result = ResolveResult {};
    result.ok = true;
    result.response.name = output.name;
    result.response.mode = output.mode;
    result.response.sourceDirectory = eacp::FilePath {output.sourceDirectory};
    result.options = output.options;

    if (output.mode != "prebuilt")
        return result;

    result.response.prefix = eacp::FilePath {output.prefix};
    result.response.platform = output.platform;
    result.response.configs = output.configs;
    result.response.note = output.note;
    result.fingerprintHash = output.fingerprintHash;
    result.compatibleKey = output.compatibleKey;
    result.anyKey = output.anyKey;
    return result;
}

BuildInput buildInputFor(const Request& request,
                         const Env& env,
                         const CacheRoot& cache,
                         const SourceSnapshot& snapshot,
                         const std::string& dependencyGraph,
                         const eacp::Vector<PackageDependency>& dependencies)
{
    auto input = BuildInput {};
    input.env = env;
    input.cacheRoot = cache.path().str();

    for (const auto& store: cache.stores())
        input.cacheStores.add(store.str());

    input.request = request;
    input.source = buildSourceFor(snapshot);
    input.dependencies = dependencies;
    input.dependencyGraph = dependencyGraph;
    input.epmExecutable = env.epmExecutable.empty()
                              ? eacp::Files::executablePath().str()
                              : env.epmExecutable;
    input.verbose = Log::isVerbose();
    return input;
}

ResolveResult resolveSnapshot(const Request& request,
                              const Env& env,
                              const CacheRoot& cache,
                              const SourceSnapshot& snapshot,
                              const std::string& dependencyGraph,
                              const eacp::Vector<PackageDependency>& dependencies)
{
    return resolveResultFor(buildInProcess(buildInputFor(
        request, env, cache, snapshot, dependencyGraph, dependencies)));
}

ResolveResult resolvePackage(const Request& request,
                             const Env& env,
                             const CacheRoot& cache,
                             bool fetchSourceDir)
{
    if (auto error = requestError(request))
        return failure(*error);

    if (request.mode != "source")
        if (auto problem = configLinkProblem(env, request.configs))
            return failure(request.name + ": " + *problem);

    // Resolved only: the step fetches the source when it builds, or when the
    // package is in source mode, not to use an entry it finds.
    const auto source =
        resolveSource(cache, request.name, request.gitUrl, request.gitRef);

    if (!source.ok)
        return failure(source.error);

    auto result = resolveSnapshot(request, env, cache, source.snapshot);

    if (!result.ok || !fetchSourceDir || result.response.mode != "prebuilt"
        || !result.response.sourceDirectory.empty())
        return result;

    // After the step, for the consumer's <name>_SOURCE_DIR alone.
    const auto fetched = fetchCommit(
        cache, request.name, request.gitUrl, request.gitRef, source.snapshot.commit);

    if (!fetched.ok)
        return failure(fetched.error);

    result.response.sourceDirectory = fetched.snapshot.directory;
    return result;
}
} // namespace epm
