#include "RecipeSdk.h"

#include <epm/Util/Paths.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>

namespace epm
{
using namespace eacp;

namespace
{
std::string identityOf(const RecipeSdk& sdk)
{
    auto lines = Vector<std::string> {sdk.compiler,
                                      sdk.compilerId,
                                      sdk.compilerFrontend,
                                      sdk.cxxStandard,
                                      sdk.cxxExtensions ? "ON" : "OFF",
                                      sdk.config,
                                      joinList(sdk.flags, " "),
                                      sdk.msvcRuntime,
                                      sdk.osxSysroot,
                                      joinList(sdk.osxArchitectures, " "),
                                      sdk.osxDeploymentTarget,
                                      joinList(sdk.includeDirectories, " "),
                                      joinList(sdk.compileDefinitions, " "),
                                      joinList(sdk.compileOptions, " "),
                                      joinList(recipeLibraries(sdk), " "),
                                      joinList(sdk.linkOptions, " ")};

    return joinList(lines, "\n");
}

// <prefix>/bin/epm: the prefix is two levels up from the real executable, so a
// symlink to it on PATH still finds the install beside it.
FilePath installPrefix(const FilePath& epmExecutable)
{
    return resolvedPath(epmExecutable).parentDirectory().parentDirectory();
}

std::optional<Vector<std::string>>
    installedArchives(const FilePath& directory, const Vector<std::string>& names)
{
    auto archives = Vector<std::string> {};

    for (const auto& name: names)
    {
        const auto archive = directory / name;

        if (!Files::isFile(archive))
            return std::nullopt;

        archives.add(archive.str());
    }

    return archives;
}
} // namespace

Vector<std::string> recipeConfigs(const RecipeSdk& sdk)
{
    auto configs = Vector<std::string> {};

    for (const auto& [config, archives]: sdk.libraries)
        configs.add(config);

    return configs;
}

Vector<std::string> recipeLibraries(const RecipeSdk& sdk)
{
    const auto found = sdk.libraries.find(sdk.config);
    return found == sdk.libraries.end() ? Vector<std::string> {} : found->second;
}

bool clCommandLine(const RecipeSdk& sdk)
{
    return sdk.compilerId == "MSVC" || sdk.compilerFrontend == "MSVC";
}

std::string msvcRuntimeFor(const RecipeSdk& sdk, std::string_view config)
{
    if (sdk.msvcRuntime.empty())
        return {};

    constexpr auto debug = std::string_view {"Debug"};
    auto runtime = sdk.msvcRuntime;

    if (const auto at = runtime.find(debug); at != std::string::npos)
        runtime.erase(at, debug.size());

    if (config != debug)
        return runtime;

    const auto dll = runtime.ends_with("DLL");
    return runtime.substr(0, runtime.size() - (dll ? 3 : 0)) + "Debug"
           + (dll ? "DLL" : "");
}

std::optional<RecipeSdk> installedRecipeSdk(const BakedRecipeSdk& baked,
                                            const FilePath& prefix)
{
    const auto includeDirectory = prefix / baked.includeDestination;

    if (!Files::isFile(includeDirectory / "epm" / "Recipe" / "Recipe.h"))
        return std::nullopt;

    auto sdk = baked.sdk;
    sdk.includeDirectories = {includeDirectory.str()};
    sdk.libraries.clear();

    for (const auto& entry: Files::listDirectory(prefix / baked.libraryDestination))
    {
        if (entry.kind != Files::EntryKind::directory)
            continue;

        if (auto archives = installedArchives(entry.path, baked.installedLibraries))
            sdk.libraries[entry.path.fileName()] = std::move(*archives);
    }

    if (!sdk.libraries.contains(sdk.config))
        return std::nullopt;

    return sdk;
}

RecipeSdk recipeSdkFor(const FilePath& epmExecutable)
{
    const auto& baked = bakedRecipeSdk();
    auto located =
        installedRecipeSdk(baked, installPrefix(epmExecutable)).value_or(baked.sdk);
    located.identity = identityOf(located);
    return located;
}

const RecipeSdk& recipeSdk()
{
    static const auto sdk = recipeSdkFor(Files::executablePath());
    return sdk;
}
} // namespace epm
