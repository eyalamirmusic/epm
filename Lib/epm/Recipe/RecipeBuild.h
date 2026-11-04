#pragma once

#include "RecipeSdk.h"

#include <epm/Cache/CacheRoot.h>

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
struct RecipeBuild
{
    bool ok = false;

    // The executable was already in the cache: nothing was compiled.
    bool cached = false;

    // <cache>/recipe/<key12>/epm-recipe (epm-recipe.exe on Windows), and
    // compile.log beside it.
    eacp::FilePath executable;
    eacp::FilePath log;

    // One line, and the end of the compiler output when it failed.
    std::string error;
    std::string details;

    // The compiler's first "error:" line when it failed, if it printed one.
    std::string firstDiagnostic;
};

// Compiles an epm.cpp into a recipe executable for runRecipe(), unless the
// cache already holds one for the same source, SDK, libraries, headers and
// epm binary. Fails on Windows: there are no recipes there until this has an
// MSVC argument shape.
RecipeBuild buildRecipe(const eacp::FilePath& source, const CacheRoot& cache);

// SHA-256 over the contents of what a recipe is built from beside its source:
// the archives it links (recipeLibraries) and epm's own headers, found under
// the include directory holding <epm/Recipe/Recipe.h>. nullopt when one of
// them cannot be read.
std::optional<std::string> recipeInputsHash(const RecipeSdk& sdk);

// The cache key: SHA-256 of the SDK identity, of this executable's own hash,
// of recipeInputsHash and of the source. An edited library or header, or a
// rebuilt epm, gets fresh recipe executables.
std::string recipeKey(const RecipeSdk& sdk,
                      std::string_view executableHash,
                      std::string_view inputsHash,
                      std::string_view source);

// The arguments after the compiler for compiling `source` into the executable
// `output`: one direct invocation, compile and link, with no CMake involved.
// The SDK's archives for its configuration and its link options follow the
// source. GCC and Clang spellings: MSVC would need its own.
eacp::Vector<std::string> recipeCompileArguments(const RecipeSdk& sdk,
                                                 const eacp::FilePath& source,
                                                 const eacp::FilePath& output);
} // namespace epm
