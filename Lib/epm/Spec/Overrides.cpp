#include "Overrides.h"

#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Strings.h>

namespace epm
{
using namespace eacp;

namespace
{
void fillString(std::string& value,
                const EnvironmentLookup& lookup,
                std::string_view variable)
{
    if (!value.empty())
        return;

    if (auto text = lookup(variable))
        value = *text;
}

void fillList(EA::Vector<std::string>& value,
              const EnvironmentLookup& lookup,
              std::string_view variable)
{
    if (!value.empty())
        return;

    if (auto text = lookup(variable))
        value = splitList(*text);
}

std::optional<bool> boolFrom(const EnvironmentLookup& lookup,
                             std::string_view variable)
{
    if (auto text = lookup(variable))
        return Strings::tryParseBool(Strings::trim(*text));

    return std::nullopt;
}

void fillPackage(PackageOverride& package,
                 const std::string& name,
                 const EnvironmentLookup& lookup)
{
    const auto prefix = "EPM_" + name + "_";

    fillString(package.mode, lookup, prefix + "MODE");
    fillString(package.source, lookup, prefix + "SOURCE");
    fillList(package.configs, lookup, prefix + "CONFIGS");

    if (auto text = lookup(prefix + "OPTIONS"))
        for (auto& [key, value]: parseOptionList(*text))
            package.options.emplace(key, value);
}
} // namespace

std::map<std::string, std::string> parseOptionList(std::string_view text)
{
    auto options = std::map<std::string, std::string> {};

    for (const auto& item: splitList(text))
    {
        const auto equals = item.find('=');

        if (equals == std::string::npos)
            options[item] = "ON";
        else
            options[item.substr(0, equals)] = item.substr(equals + 1);
    }

    return options;
}

Overrides withEnvironment(Overrides fromCMake,
                          const EA::Vector<std::string>& names,
                          const EnvironmentLookup& lookup)
{
    auto overrides = std::move(fromCMake);

    fillString(overrides.mode, lookup, "EPM_MODE");
    fillList(overrides.configs, lookup, "EPM_CONFIGS");
    fillString(overrides.abiMatch, lookup, "EPM_ABI_MATCH");

    if (boolFrom(lookup, "EPM_STRICT") == true)
        overrides.strict = true;

    if (!overrides.buildMissingBinaries)
        overrides.buildMissingBinaries =
            boolFrom(lookup, "EPM_BUILD_MISSING_BINARIES");

    for (const auto& name: names)
    {
        auto package = overrides.packages[name];
        fillPackage(package, name, lookup);

        if (package.mode.empty() && package.source.empty() && package.configs.empty()
            && package.options.empty())
            overrides.packages.erase(name);
        else
            overrides.packages[name] = std::move(package);
    }

    return overrides;
}
} // namespace epm
