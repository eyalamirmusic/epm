#pragma once

// The header an epm.cpp includes. epm compiles the file into a module with the
// toolchain that built epm (RecipeBuild.h) and loads it into its own process
// (RecipeLoader.h). The module links nothing: what it calls resolves against
// the epm executable, so it may use anything epm-core and eacp-core offer.
//
//     #include <epm/Recipe/Recipe.h>
//
//     using namespace epm;
//
//     struct App : Recipe
//     {
//         void describe(Project& project, const Env& env) override
//         {
//             project.add("fmt", "fmtlib/fmt", "11.0.2");
//         }
//     };
//
//     EPM_RECIPE(App)

#include "Project.h"

#include <epm/Spec/Env.h>

namespace epm
{
// Bumped whenever Recipe, Project or RecipeEntry change in a way an already
// compiled recipe would misread. A recipe is rebuilt per epm binary anyway;
// this catches a module built against another epm's headers.
constexpr int recipeApiVersion = 2;

struct Recipe
{
    virtual ~Recipe() = default;
    virtual void describe(Project& project, const Env& env) = 0;
};

// What a recipe module's one entry point returns. A plain C struct, without
// member initialisers, so that an extern "C" function may return it.
struct RecipeEntry
{
    int apiVersion;
    Recipe* (*create)();
};

inline constexpr auto recipeEntrySymbol = "epmRecipeEntry";
} // namespace epm

#ifdef _WIN32
#define EPM_RECIPE_EXPORT __declspec(dllexport)
#else
#define EPM_RECIPE_EXPORT __attribute__((visibility("default")))
#endif

// Names the recipe's Recipe type, once per epm.cpp, at global scope.
#define EPM_RECIPE(Type)                                                            \
    extern "C" EPM_RECIPE_EXPORT epm::RecipeEntry epmRecipeEntry()                  \
    {                                                                               \
        return {epm::recipeApiVersion, []() -> epm::Recipe* { return new Type; }};  \
    }
