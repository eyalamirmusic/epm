#include "RecipeLoader.h"

#include <exception>

namespace epm
{
using namespace eacp;

namespace
{
RecipeLoad loadFailure(std::string error)
{
    auto load = RecipeLoad {};
    load.error = std::move(error);
    return load;
}
} // namespace

LoadedRecipe::LoadedRecipe(Plugins::DynamicLibrary libraryToUse,
                           OwningPointer<Recipe> recipeToUse)
    : library(std::move(libraryToUse))
    , recipe(std::move(recipeToUse))
{
}

LoadedRecipe::~LoadedRecipe()
{
    recipe.reset();
}

LoadedRecipe& LoadedRecipe::operator=(LoadedRecipe&& other) noexcept
{
    recipe.reset();
    library = std::move(other.library);
    recipe = std::move(other.recipe);
    return *this;
}

Project LoadedRecipe::describe(const Env& env)
{
    auto project = Project {};
    recipe->describe(project, env);
    return project;
}

RecipeLoad loadRecipe(const FilePath& module)
{
    auto library = Plugins::DynamicLibrary {};

    if (!library.open(module))
        return loadFailure("cannot load the recipe module " + module.str() + ": "
                           + library.lastError());

    using EntryFunction = RecipeEntry (*)();
    const auto entryFunction =
        library.findFunction<EntryFunction>(recipeEntrySymbol);

    if (entryFunction == nullptr)
        return loadFailure("the recipe module " + module.str() + " has no "
                           + recipeEntrySymbol
                           + "(): does its epm.cpp end with EPM_RECIPE(<type>)?");

    const auto entry = entryFunction();

    if (entry.apiVersion != recipeApiVersion)
        return loadFailure("the recipe module " + module.str()
                           + " was built for recipe API "
                           + std::to_string(entry.apiVersion) + ", this epm has "
                           + std::to_string(recipeApiVersion));

    if (entry.create == nullptr)
        return loadFailure("the recipe module " + module.str()
                           + " has no Recipe factory");

    auto recipe = OwningPointer<Recipe> {};

    try
    {
        recipe = entry.create();
    }
    catch (const std::exception& error)
    {
        return loadFailure("the recipe in " + module.str()
                           + " threw on creation: " + error.what());
    }

    if (recipe == nullptr)
        return loadFailure("the recipe module " + module.str() + " made no Recipe");

    auto load = RecipeLoad {};
    load.recipe.emplace(std::move(library), std::move(recipe));
    return load;
}
} // namespace epm
