#pragma once

#include <epm/Recipe/RecipeSdk.h>

#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace epm
{
// recipe-sdk.cmake, written by `epm recipe-sdk` for epm.cmake's epm::recipe
// target: EPM_RECIPE_COMPILER_ID, EPM_RECIPE_CXX_STANDARD,
// EPM_RECIPE_CXX_EXTENSIONS (ON or OFF), EPM_RECIPE_INCLUDE_DIRS (where the
// embedded headers were written), and the lists
// EPM_RECIPE_COMPILE_DEFINITIONS and EPM_RECIPE_COMPILE_OPTIONS (epm-core's
// warning flags, meant for the compiler named by EPM_RECIPE_COMPILER_ID only).
std::string recipeSdkCMakeText(const RecipeSdk& sdk,
                               const eacp::FilePath& includeDirectory);

void writeRecipeSdkCMake(const eacp::FilePath& path,
                         const RecipeSdk& sdk,
                         const eacp::FilePath& includeDirectory);
} // namespace epm
