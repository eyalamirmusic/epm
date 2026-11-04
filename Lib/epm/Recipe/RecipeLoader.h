#pragma once

#include "Recipe.h"

#include <eacp/Core/Plugins/DynamicLibrary.h>
#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>

namespace epm
{
// A recipe module loaded into this process, and the Recipe it made. The
// Project a describe() returns holds only data, so it outlives this.
class LoadedRecipe
{
public:
    LoadedRecipe(eacp::Plugins::DynamicLibrary libraryToUse,
                 eacp::OwningPointer<Recipe> recipeToUse);
    ~LoadedRecipe();

    LoadedRecipe(LoadedRecipe&& other) noexcept = default;
    LoadedRecipe& operator=(LoadedRecipe&& other) noexcept;

    // Throws what the recipe throws.
    Project describe(const Env& env);

private:
    // Declared first, destroyed last: the recipe's code lives in it.
    eacp::Plugins::DynamicLibrary library;
    eacp::OwningPointer<Recipe> recipe;
};

struct RecipeLoad
{
    std::optional<LoadedRecipe> recipe;
    std::string error;
};

// Loads a module from buildRecipe() and creates its Recipe. Fails when the
// loader refuses it (an unresolved symbol, say), when it has no EPM_RECIPE
// entry point, or when it was built for another recipeApiVersion.
RecipeLoad loadRecipe(const eacp::FilePath& module);
} // namespace epm
