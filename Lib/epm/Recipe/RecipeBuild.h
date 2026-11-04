#pragma once

#include "RecipeSdk.h"

#include <epm/Cache/CacheRoot.h>
#include <epm/Spec/Env.h>

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <string_view>

namespace epm
{
struct RecipeBuild
{
    bool ok = false;

    // The module was already in the cache: nothing was compiled.
    bool cached = false;

    // <cache>/recipe/<key12>/epm-recipe.<dylib|so>, and compile.log beside it.
    eacp::FilePath module;
    eacp::FilePath log;

    // One line, and the end of the compiler output when it failed.
    std::string error;
    std::string details;

    // The compiler's first "error:" line when it failed, if it printed one.
    std::string firstDiagnostic;
};

// What a recipe is compiled with on this machine: the C++ compiler and macOS
// SDK of the configure that asked for it (env.json), and the directory the
// headers embedded in this binary were written to.
struct RecipeToolchain
{
    std::string compiler;
    std::string compilerId;
    std::string osxSysroot;
    eacp::FilePath includeDirectory;
};

// <cache>/recipe-sdk/<binary hash>/include: the headers a recipe includes,
// embedded in this binary and written there on first use. Throws when they
// cannot be written, or when this binary embeds none.
eacp::FilePath writeRecipeHeaders(const CacheRoot& cache);

// The toolchain `env` names, with writeRecipeHeaders' directory.
RecipeToolchain recipeToolchainFor(const Env& env, const CacheRoot& cache);

// Compiles an epm.cpp into a module that loadRecipe() loads into this process,
// unless the cache already holds one for the same source, toolchain, SDK and
// epm binary. Fails on Windows, which has no recipes yet, and when `env`
// names no C++ compiler.
RecipeBuild buildRecipe(const eacp::FilePath& source,
                        const CacheRoot& cache,
                        const Env& env);

// The cache key: SHA-256 of the SDK identity, of the compiler, of this
// executable's own hash and of the source.
std::string recipeKey(const RecipeSdk& sdk,
                      const RecipeToolchain& toolchain,
                      std::string_view executableHash,
                      std::string_view source);

// The arguments after the compiler for compiling `source` into `module`: one
// direct invocation, compile and link, with no CMake involved.
eacp::Vector<std::string> recipeCompileArguments(const RecipeSdk& sdk,
                                                 const RecipeToolchain& toolchain,
                                                 const eacp::FilePath& source,
                                                 const eacp::FilePath& module);
} // namespace epm
