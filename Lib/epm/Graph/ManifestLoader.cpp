#include "ManifestLoader.h"

#include <epm/Recipe/RecipeBuild.h>
#include <epm/Recipe/RecipeLoader.h>
#include <epm/Util/Log.h>

#include <eacp/Core/Utils/Files.h>

#include <exception>

namespace epm
{
using namespace eacp;

namespace
{
std::string buildError(const RecipeBuild& build)
{
    if (build.firstDiagnostic.empty())
        return build.error;

    return build.error + "\n  " + build.firstDiagnostic;
}
} // namespace

RecipeManifestLoader::RecipeManifestLoader(CacheRoot cacheToUse)
    : cache(std::move(cacheToUse))
{
}

std::optional<Project> RecipeManifestLoader::load(const FilePath& directory,
                                                  const Env& env,
                                                  std::string& error)
{
    const auto recipe = directory / manifestFileName;

    if (!Files::isFile(recipe))
        return Project {};

    return loadFile(recipe, env, error);
}

std::optional<Project> RecipeManifestLoader::loadFile(const FilePath& recipe,
                                                      const Env& env,
                                                      std::string& error)
{
    const auto build = buildRecipe(recipe, cache, env);

    if (!build.ok)
    {
        error = buildError(build);
        return std::nullopt;
    }

    Log::step((build.cached ? "cached " : "built ") + build.module.str());

    auto load = loadRecipe(build.module);

    if (!load.recipe)
    {
        error = load.error;
        return std::nullopt;
    }

    try
    {
        return load.recipe->describe(env);
    }
    catch (const std::exception& thrown)
    {
        error = "the recipe " + recipe.str() + " threw: " + thrown.what();
    }
    catch (...)
    {
        error = "the recipe " + recipe.str() + " threw";
    }

    return std::nullopt;
}
} // namespace epm
