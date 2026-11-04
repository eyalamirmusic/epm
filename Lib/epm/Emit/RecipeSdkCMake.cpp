#include "RecipeSdkCMake.h"
#include "Response.h"

#include <epm/Util/Text.h>

namespace epm
{
std::string recipeSdkCMakeText(const RecipeSdk& sdk)
{
    const auto set = [](std::string_view variable, const std::string& value)
    { return "set(EPM_RECIPE_" + std::string {variable} + " " + value + ")\n"; };

    return "# Written by `epm recipe-sdk`: how this epm compiles an epm.cpp.\n"
           + set("CXX_COMPILER", cmakeQuoted(sdk.compiler))
           + set("COMPILER_ID", cmakeQuoted(sdk.compilerId))
           + set("CXX_STANDARD", cmakeQuoted(sdk.cxxStandard))
           + set("CXX_EXTENSIONS", sdk.cxxExtensions ? "ON" : "OFF")
           + set("INCLUDE_DIRS", cmakeQuotedList(sdk.includeDirectories))
           + set("COMPILE_DEFINITIONS", cmakeQuotedList(sdk.compileDefinitions))
           + set("COMPILE_OPTIONS", cmakeQuotedList(sdk.compileOptions));
}

bool writeRecipeSdkCMake(const eacp::FilePath& path, const RecipeSdk& sdk)
{
    return writeTextFile(path, recipeSdkCMakeText(sdk));
}
} // namespace epm
