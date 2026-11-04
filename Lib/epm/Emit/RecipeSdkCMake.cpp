#include "RecipeSdkCMake.h"
#include "Response.h"

#include <epm/Util/Text.h>

#include <algorithm>
#include <cctype>

namespace epm
{
namespace
{
// A configuration as CMake spells it in a <CONFIG> suffix. (Upstream
// candidate: eacp's pending Strings::toUpper.)
std::string configSuffix(std::string config)
{
    std::ranges::transform(config,
                           config.begin(),
                           [](unsigned char c) { return char(std::toupper(c)); });
    return config;
}
} // namespace

std::string recipeSdkCMakeText(const RecipeSdk& sdk)
{
    const auto set = [](std::string_view variable, const std::string& value)
    { return "set(EPM_RECIPE_" + std::string {variable} + " " + value + ")\n"; };

    auto text = "# Written by `epm recipe-sdk`: how this epm compiles an epm.cpp.\n"
                + set("CXX_COMPILER", cmakeQuoted(sdk.compiler))
                + set("COMPILER_ID", cmakeQuoted(sdk.compilerId))
                + set("CXX_STANDARD", cmakeQuoted(sdk.cxxStandard))
                + set("CXX_EXTENSIONS", sdk.cxxExtensions ? "ON" : "OFF")
                + set("INCLUDE_DIRS", cmakeQuotedList(sdk.includeDirectories))
                + set("COMPILE_DEFINITIONS", cmakeQuotedList(sdk.compileDefinitions))
                + set("COMPILE_OPTIONS", cmakeQuotedList(sdk.compileOptions))
                + set("CONFIGS", cmakeQuotedList(recipeConfigs(sdk)));

    for (const auto& [config, libraries]: sdk.libraries)
        text += set("LINK_LIBRARIES_" + configSuffix(config),
                    cmakeQuotedList(libraries));

    return text + set("LINK_OPTIONS", cmakeQuotedList(sdk.linkOptions));
}

bool writeRecipeSdkCMake(const eacp::FilePath& path, const RecipeSdk& sdk)
{
    return writeTextFile(path, recipeSdkCMakeText(sdk));
}
} // namespace epm
