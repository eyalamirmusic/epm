#include "Configs.h"

#include <epm/Spec/Platform.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Strings.h>

namespace epm
{
using namespace eacp;

namespace
{
const auto standardConfigs =
    Vector<std::string> {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"};

std::optional<std::string> standardSpelling(std::string_view name)
{
    for (const auto& config: standardConfigs)
        if (Strings::equalsCaseInsensitive(config, name))
            return config;

    return std::nullopt;
}

int canonicalRank(const std::string& name)
{
    const auto index = standardConfigs.getIndexOf(name);
    return index < 0 ? standardConfigs.size() : index;
}
} // namespace

std::optional<std::string> buildConfigFor(std::string_view name)
{
    const auto config = standardSpelling(name);

    if (!config)
        return std::nullopt;

    if (*config == "Debug" || *config == "RelWithDebInfo")
        return config;

    return "Release";
}

bool isDebugConfig(std::string_view name)
{
    return buildConfigFor(name) == "Debug";
}

Vector<std::string> canonicalConfigs(const Vector<std::string>& names)
{
    auto unique = Vector<std::string> {};

    for (const auto& name: names)
        unique.addIfNotThere(standardSpelling(name).value_or(name));

    unique.stableSort([](const std::string& a, const std::string& b)
                      { return canonicalRank(a) < canonicalRank(b); });
    return unique;
}

Vector<std::string> buildConfigsFor(const Vector<std::string>& names)
{
    auto configs = Vector<std::string> {};

    for (const auto& name: names)
        if (auto config = buildConfigFor(name))
            configs.add(*config);

    return canonicalConfigs(configs);
}

Vector<std::string> missingConfigs(const Vector<std::string>& present,
                                   const Vector<std::string>& wanted)
{
    const auto have = canonicalConfigs(present);
    auto missing = Vector<std::string> {};

    for (const auto& config: canonicalConfigs(wanted))
        if (!have.contains(config))
            missing.add(config);

    return missing;
}

std::string mapConsumerConfig(std::string_view consumer,
                              const Vector<std::string>& available)
{
    for (const auto& config: available)
        if (Strings::equalsCaseInsensitive(config, consumer))
            return config;

    for (const auto* preferred: {"Release", "RelWithDebInfo"})
        if (available.contains(preferred))
            return preferred;

    return available[0];
}

std::optional<std::string> configLinkProblem(const Env& env,
                                             const Vector<std::string>& available)
{
    if (!msvcAbi(env) || available.empty()
        || !msvcRuntimeHasDebug(msvcRuntimeOf(env)))
        return std::nullopt;

    const auto builds = env.isMultiConfig ? env.configurationTypes
                                          : Vector<std::string> {env.buildType};

    for (const auto& build: builds)
    {
        const auto linked = mapConsumerConfig(build, available);

        if (isDebugConfig(build) == isDebugConfig(linked))
            continue;

        const auto name = build.empty() ? std::string {"Release"} : build;
        const auto kind = isDebugConfig(build) ? "Debug" : "Release";
        return "a " + name + " build of this project cannot link its " + linked
               + " build on the MSVC ABI, where the debug and release runtimes do "
                 "not mix. Add "
               + kind
               + " to its configs (releaseIfPossible() in a recipe, or "
                 "EPM_CONFIGS), or give the "
               + name + " build the release runtime (CMAKE_MSVC_RUNTIME_LIBRARY)";
    }

    return std::nullopt;
}

std::string exportNamespace(std::string_view config)
{
    return "epm_" + Strings::toLower(std::string {config}) + "::";
}

std::string configSelector(const Vector<std::string>& available,
                           std::string_view target)
{
    const auto configs = canonicalConfigs(available);
    const auto targetIn = [&](const std::string& config)
    { return exportNamespace(config) + std::string {target}; };

    // What a consumer config outside every list below gets.
    const auto fallback = mapConsumerConfig({}, configs);

    auto consumers = standardConfigs;

    for (const auto& config: configs)
        consumers.addIfNotThere(config);

    auto expression = targetIn(fallback);

    for (auto i = configs.size() - 1; i >= 0; --i)
    {
        const auto& config = configs[i];

        if (config == fallback)
            continue;

        auto mapped = Vector<std::string> {};

        for (const auto& consumer: consumers)
            if (mapConsumerConfig(consumer, configs) == config)
                mapped.add(consumer);

        expression = "$<IF:$<CONFIG:" + joinList(mapped, ",") + ">,"
                     + targetIn(config) + "," + expression + ">";
    }

    return expression;
}
} // namespace epm
