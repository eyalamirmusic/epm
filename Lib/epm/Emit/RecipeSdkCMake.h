#pragma once

#include <epm/Recipe/RecipeSdk.h>

#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace epm
{
// recipe-sdk.cmake, written by `epm recipe-sdk` for epm.cmake's epm::recipe
// target: EPM_RECIPE_CXX_COMPILER, EPM_RECIPE_COMPILER_ID,
// EPM_RECIPE_CXX_STANDARD, EPM_RECIPE_CXX_EXTENSIONS (ON or OFF), and the
// lists EPM_RECIPE_INCLUDE_DIRS, EPM_RECIPE_COMPILE_DEFINITIONS,
// EPM_RECIPE_COMPILE_OPTIONS (epm-recipe's warning flags, meant for the
// compiler named by EPM_RECIPE_COMPILER_ID only), EPM_RECIPE_CONFIGS (the
// configurations whose libraries this epm has, e.g. Debug;Release),
// EPM_RECIPE_LINK_LIBRARIES_<CONFIG> per configuration, upper case as in
// CMake's own <CONFIG> suffixes (absolute archive paths in link order), and
// EPM_RECIPE_LINK_OPTIONS (frameworks and system libraries, as
// target_link_libraries takes them; may be empty).
std::string recipeSdkCMakeText(const RecipeSdk& sdk);

bool writeRecipeSdkCMake(const eacp::FilePath& path, const RecipeSdk& sdk);
} // namespace epm
