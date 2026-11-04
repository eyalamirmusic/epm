#include "Merge.h"

#include <epm/Build/Configs.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Strings.h>

namespace epm
{
using namespace eacp;

namespace
{
using Options = std::map<std::string, std::string>;

// One source of values, in the shape every source fits.
struct Layer
{
    std::string label;
    std::string git;
    std::string tag;
    std::string mode;
    std::string abiMatch;
    EA::Vector<std::string> configs;
    Options options;
};

Layer manifestLayer(std::string label, const ManifestPackage& package)
{
    return {std::move(label),
            package.git,
            package.tag,
            package.mode,
            package.abiMatch,
            package.configs,
            package.options};
}

EA::Vector<Layer> layersOf(const std::string& name, const MergeLayers& layers)
{
    const auto& package = layers.packageOverride;
    const auto& overrides = layers.overrides;
    const auto& profile = layers.profile;
    const auto profileLabel = "profile:" + layers.profileName;

    auto result = EA::Vector<Layer> {};
    result.add({"override:package",
                {},
                {},
                package.mode,
                {},
                package.configs,
                package.options});
    result.add({"override",
                {},
                {},
                overrides.mode,
                overrides.abiMatch,
                overrides.configs,
                {}});

    if (const auto* found = findPackage(profile.packages, name))
        result.add({profileLabel + ":package",
                    {},
                    {},
                    found->mode,
                    found->abiMatch,
                    found->configs,
                    found->options});

    result.add(
        {profileLabel, {}, {}, profile.mode, profile.abiMatch, profile.configs, {}});

    if (layers.root)
        result.add(manifestLayer(rootDeclarer, *layers.root));

    if (layers.dependency)
        result.add(manifestLayer("manifest:" + layers.dependency->declarer,
                                 layers.dependency->package));

    return result;
}

template <typename T>
T firstSet(const EA::Vector<Layer>& layers,
           T Layer::* member,
           const std::string& key,
           T fallback,
           Provenance& provenance)
{
    for (const auto& layer: layers)
    {
        if (!(layer.*member).empty())
        {
            provenance[key] = layer.label;
            return layer.*member;
        }
    }

    provenance[key] = "default";
    return fallback;
}

Options mergedOptions(const EA::Vector<Layer>& layers, Provenance& provenance)
{
    auto options = Options {};

    for (const auto& layer: layers)
        for (const auto& [key, value]: layer.options)
            if (options.emplace(key, value).second)
                provenance["options." + key] = layer.label;

    return options;
}
} // namespace

bool isAutoConfigs(const EA::Vector<std::string>& configs)
{
    return configs.empty()
           || (configs.size() == 1 && Strings::toLower(configs[0]) == "auto");
}

std::optional<EA::Vector<std::string>>
    resolveConfigs(const EA::Vector<std::string>& configs, const Env& env)
{
    if (isAutoConfigs(configs))
    {
        if (env.isMultiConfig)
            return EA::Vector<std::string> {"Debug", "Release"};

        if (Strings::toLower(env.buildType) == "debug")
            return EA::Vector<std::string> {"Debug"};

        return EA::Vector<std::string> {"Release"};
    }

    for (const auto& config: configs)
        if (!buildConfigFor(config))
            return std::nullopt;

    return buildConfigsFor(configs);
}

MergeResult
    mergeRequest(const std::string& name, const MergeLayers& layers, const Env& env)
{
    const auto all = layersOf(name, layers);
    auto result = MergeResult {};
    auto& effective = result.effective;
    auto& request = effective.request;
    auto& provenance = effective.provenance;

    request.name = name;
    request.gitUrl =
        expandGitUrl(firstSet(all, &Layer::git, "git", std::string {}, provenance));
    request.gitRef = firstSet(all, &Layer::tag, "tag", std::string {}, provenance);
    request.mode =
        firstSet(all, &Layer::mode, "mode", std::string {"auto"}, provenance);
    request.abiMatch = firstSet(
        all, &Layer::abiMatch, "abiMatch", std::string {"compatible"}, provenance);
    request.options = mergedOptions(all, provenance);

    const auto configs = firstSet(
        all, &Layer::configs, "configs", EA::Vector<std::string> {}, provenance);
    const auto resolved = resolveConfigs(configs, env);

    if (!resolved)
    {
        result.error = name + " has an unknown configuration in configs '"
                       + joinList(configs, ";") + "' (from " + provenance["configs"]
                       + "): give auto, or Debug, Release, RelWithDebInfo and "
                         "MinSizeRel";
        return result;
    }

    request.configs = *resolved;

    const auto& buildMissing = layers.overrides.buildMissingBinaries;
    request.buildMissingBinaries = buildMissing.value_or(true);
    provenance["buildMissingBinaries"] = buildMissing ? "override" : "default";

    if (!layers.packageOverride.source.empty())
    {
        effective.source = layers.packageOverride.source;
        provenance["source"] = "override:package";
        request.mode = "source";
        provenance["mode"] = "override:package";
    }

    result.ok = true;
    return result;
}
} // namespace epm
