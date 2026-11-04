#include "RecipeSdkCMake.h"
#include "Response.h"

#include <eacp/Core/Utils/Files.h>

namespace epm
{
std::string recipeSdkCMakeText(const RecipeSdk& sdk,
                               const eacp::FilePath& includeDirectory)
{
    const auto set = [](std::string_view variable, const std::string& value)
    { return "set(EPM_RECIPE_" + std::string {variable} + " " + value + ")\n"; };

    return "# Written by `epm recipe-sdk`: how this epm compiles an epm.cpp.\n"
           + set("COMPILER_ID", cmakeQuoted(sdk.compilerId))
           + set("CXX_STANDARD", cmakeQuoted(sdk.cxxStandard))
           + set("CXX_EXTENSIONS", sdk.cxxExtensions ? "ON" : "OFF")
           + set("INCLUDE_DIRS", cmakeQuoted(includeDirectory.str()))
           + set("COMPILE_DEFINITIONS", cmakeQuotedList(sdk.compileDefinitions))
           + set("COMPILE_OPTIONS", cmakeQuotedList(sdk.compileOptions));
}

void writeRecipeSdkCMake(const eacp::FilePath& path,
                         const RecipeSdk& sdk,
                         const eacp::FilePath& includeDirectory)
{
    eacp::Files::writeFileAtomically(path,
                                     recipeSdkCMakeText(sdk, includeDirectory));
}
} // namespace epm
