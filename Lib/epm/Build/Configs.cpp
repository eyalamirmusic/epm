#include "Configs.h"

#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Strings.h>

namespace epm
{
using namespace eacp;

namespace
{
const auto standardConfigs =
    EA::Vector<std::string> {"Debug", "Release", "RelWithDebInfo", "MinSizeRel"};

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

    if (*config == "Debug")
        return config;

    return "Release";
}

EA::Vector<std::string> canonicalConfigs(const EA::Vector<std::string>& names)
{
    auto unique = EA::Vector<std::string> {};

    for (const auto& name: names)
        unique.addIfNotThere(standardSpelling(name).value_or(name));

    unique.stableSort([](const std::string& a, const std::string& b)
                      { return canonicalRank(a) < canonicalRank(b); });
    return unique;
}

EA::Vector<std::string> buildConfigsFor(const EA::Vector<std::string>& names)
{
    auto configs = EA::Vector<std::string> {};

    for (const auto& name: names)
        if (auto config = buildConfigFor(name))
            configs.add(*config);

    return canonicalConfigs(configs);
}

EA::Vector<std::string> missingConfigs(const EA::Vector<std::string>& present,
                                       const EA::Vector<std::string>& wanted)
{
    const auto have = canonicalConfigs(present);
    auto missing = EA::Vector<std::string> {};

    for (const auto& config: canonicalConfigs(wanted))
        if (!have.contains(config))
            missing.add(config);

    return missing;
}

std::string mapConsumerConfig(std::string_view consumer,
                              const EA::Vector<std::string>& available)
{
    for (const auto& config: available)
        if (Strings::equalsCaseInsensitive(config, consumer))
            return config;

    if (available.contains("Release"))
        return "Release";

    return available[0];
}

std::string exportNamespace(std::string_view config)
{
    return "epm_" + Strings::toLower(std::string {config}) + "::";
}

std::string configSelector(const EA::Vector<std::string>& available,
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

        auto mapped = EA::Vector<std::string> {};

        for (const auto& consumer: consumers)
            if (mapConsumerConfig(consumer, configs) == config)
                mapped.add(consumer);

        expression = "$<IF:$<CONFIG:" + joinList(mapped, ",") + ">,"
                     + targetIn(config) + "," + expression + ">";
    }

    return expression;
}
} // namespace epm
