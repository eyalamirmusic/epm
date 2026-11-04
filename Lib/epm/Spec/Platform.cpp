#include "Platform.h"

#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Strings.h>

namespace epm
{
using namespace eacp;

namespace
{
std::string architectureName(std::string_view processor)
{
    const auto lower = Strings::toLower(std::string {processor});

    if (lower == "x86_64" || lower == "amd64" || lower == "x64")
        return "x64";

    if (lower == "aarch64" || lower == "arm64")
        return "arm64";

    return lower;
}

// CMAKE_OSX_ARCHITECTURES when set, else the processor; "universal" when it
// holds both arm64 and x86_64.
std::string appleArchitecture(const Env& env)
{
    auto architectures = Vector<std::string> {};

    for (const auto& architecture: splitList(env.osxArchitectures))
        architectures.addIfNotThere(architectureName(architecture));

    if (architectures.contains("arm64") && architectures.contains("x64"))
        return "universal";

    if (architectures.size() == 1)
        return architectures[0];

    return architectureName(env.systemProcessor);
}

bool isSimulatorSysroot(const std::string& sysroot)
{
    return Strings::toLower(sysroot).find("simulator") != std::string::npos;
}

bool isAppleEmbedded(const std::string& system)
{
    return system == "iOS" || system == "tvOS" || system == "watchOS"
           || system == "visionOS";
}
} // namespace

std::string platformName(const Env& env)
{
    const auto system = env.systemName;

    if (system == "Darwin")
        return "macos-" + appleArchitecture(env);

    if (isAppleEmbedded(system))
        return Strings::toLower(system)
               + (isSimulatorSysroot(env.osxSysroot) ? "-simulator-" : "-")
               + appleArchitecture(env);

    if (system == "Windows")
        return "windows-" + architectureName(env.systemProcessor);

    if (system == "Linux")
        return "linux-" + architectureName(env.systemProcessor);

    if (system == "Android")
        return "android-"
               + (env.androidAbi.empty() ? Strings::toLower(env.systemProcessor)
                                         : env.androidAbi);

    return Strings::toLower(system.empty() ? std::string {"unknown"} : system) + "-"
           + Strings::toLower(env.systemProcessor);
}

bool crossCompilesWithoutToolchain(const Env& env)
{
    return env.crossCompiling && env.toolchainFile.empty()
           && !env.systemName.empty();
}
} // namespace epm
