#include "RecipeConfigure.h"
#include "Recipe.h"

#include <epm/Emit/GraphResponse.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Log.h>
#include <epm/Util/Paths.h>

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <exception>
#include <filesystem>
#include <system_error>

namespace epm
{
using namespace eacp;

namespace
{
std::unique_ptr<Package> packageToBuild(std::unique_ptr<Recipe> recipe,
                                        const BuildInput& input)
{
    const auto& name = input.request.name;

    if (!input.recipeName.empty() && name == input.recipeName)
        return recipe;

    if (auto package = recipe->createPackage(name))
        return package;

    if (input.customBuild)
        throw BuildError {"epm expects custom build steps for " + name
                          + " (add<T>) from this recipe, but its describe() "
                            "registers none for it"};

    return std::make_unique<CMakePackage>();
}

BuildOutput
    failedStep(const std::string& name, std::string error, std::string details = {})
{
    auto output = BuildOutput {};
    output.name = name;
    output.error = std::move(error);
    output.details = std::move(details);
    return output;
}

ConfigureResult configureFailure(std::string error, std::string details = {})
{
    auto result = ConfigureResult {};
    result.error = std::move(error);
    result.details = std::move(details);
    return result;
}
} // namespace

std::unique_ptr<Recipe> describedRecipe(RecipeFactory create,
                                        const BuildInput& input)
{
    auto recipe = create();
    PackageRunner::setInput(*recipe, input);
    recipe->describe();
    return recipe;
}

BuildOutput runRecipeStep(RecipeFactory create, const BuildInput& input)
{
    const auto& name = input.request.name;

    try
    {
        auto steps = packageToBuild(describedRecipe(create, input), input);
        return runPackage(*steps, input);
    }
    catch (const BuildError& error)
    {
        return failedStep(name, error.what(), error.details);
    }
    catch (const std::exception& error)
    {
        return failedStep(name, std::string {"the recipe threw: "} + error.what());
    }
    catch (...)
    {
        return failedStep(name, "the recipe threw");
    }
}

ConfigureArgs absoluteConfigureArgs(ConfigureArgs args)
{
    for (auto* path: {&args.manifest, &args.env, &args.overrides, &args.out})
        *path = resolvedPath(*path);

    if (!args.cache.empty())
        args.cache = resolvedPath(args.cache);

    return args;
}

// eacp has FilePath::currentDirectory but no setter (upstream candidate).
bool enterManifestDirectory(const FilePath& manifest, std::string& error)
{
    if (!Files::isFile(manifest))
    {
        error = "no manifest at " + manifest.str();
        return false;
    }

    const auto directory = resolvedPath(manifest).parentDirectory();
    auto failure = std::error_code {};
    std::filesystem::current_path(toStdPath(directory), failure);

    if (failure)
    {
        error = "cannot enter the manifest's directory " + directory.str() + ": "
                + failure.message();
        return false;
    }

    return true;
}

CacheRoot configureCache(const ConfigureArgs& args)
{
    const auto cache =
        args.cache.empty() ? CacheRoot::fromEnvironment() : CacheRoot {args.cache};
    Files::createDirectories(cache.path());
    return CacheRoot {resolvedPath(cache.path())};
}

FilePath configureStepsDirectory(const ConfigureArgs& args)
{
    return resolvedPath(args.out) / "steps";
}

std::optional<GraphInputs> configureInputs(const ConfigureArgs& args,
                                           std::string& error)
{
    auto inputs = GraphInputs {};
    inputs.environment = getEnv;
    inputs.rootRecipe = args.manifest;

    if (!Files::isFile(args.manifest))
    {
        error = "no manifest at " + args.manifest.str();
        return std::nullopt;
    }

    if (auto loaded = loadJsonFile<Env>(args.env))
        inputs.env = std::move(*loaded);
    else
    {
        error = "cannot read the env file " + args.env.str();
        return std::nullopt;
    }

    if (auto loaded = loadJsonFile<Overrides>(args.overrides))
        inputs.overrides = std::move(*loaded);
    else
    {
        error = "cannot read the overrides file " + args.overrides.str();
        return std::nullopt;
    }

    return inputs;
}

ConfigureResult runConfigure(const ConfigureArgs& args, ManifestLoader& loader)
{
    try
    {
        auto error = std::string {};
        auto inputs = configureInputs(args, error);

        if (!inputs)
            return configureFailure(error);

        if (auto project = loader.loadFile(args.manifest, inputs->env, error))
            inputs->project = std::move(*project);
        else
            return configureFailure("the manifest " + args.manifest.str() + ": "
                                    + error);

        const auto result = configureGraph(*inputs, configureCache(args), loader);

        if (!result.ok)
            return configureFailure(result.error, result.details);

        if (!writeGraphJson(args.out / "graph.json", result.graph))
            return configureFailure("cannot write "
                                    + (args.out / "graph.json").str());

        if (!writeGraphCMake(args.out / "graph.cmake", result.graph))
            return configureFailure("cannot write "
                                    + (args.out / "graph.cmake").str());
    }
    catch (const std::exception& error)
    {
        return configureFailure(error.what());
    }

    auto result = ConfigureResult {};
    result.ok = true;
    return result;
}

SelfRecipeLoader::SelfRecipeLoader(RecipeFactory createToUse,
                                   const FilePath& recipeToUse,
                                   CacheRoot cache,
                                   FilePath stepsDirectoryToUse)
    : create(createToUse)
    , recipe(resolvedPath(recipeToUse))
    , stepsDirectory(stepsDirectoryToUse)
    , others(std::move(cache), std::move(stepsDirectoryToUse))
{
}

bool SelfRecipeLoader::isSelf(const FilePath& file) const
{
    return resolvedPath(file) == recipe;
}

std::optional<Project> SelfRecipeLoader::load(const FilePath& directory,
                                              const Env& env,
                                              std::string& error)
{
    if (isSelf(directory / manifestFileName))
        return describeSelf(env, error);

    return others.load(directory, env, error);
}

std::optional<Project> SelfRecipeLoader::loadFile(const FilePath& file,
                                                  const Env& env,
                                                  std::string& error)
{
    if (isSelf(file))
        return describeSelf(env, error);

    return others.loadFile(file, env, error);
}

std::optional<BuildOutput> SelfRecipeLoader::build(const FilePath& file,
                                                   const BuildInput& input,
                                                   std::string& error)
{
    if (!isSelf(file))
        return others.build(file, input, error);

    if (!stepsDirectory.empty())
        return runSelfStep(stepsDirectory, input, error);

    const auto scratch = TemporaryFile {};
    return runSelfStep(scratch.path(), input, error);
}

// As the recipe executable's describe command, which runRecipe would run.
std::optional<Project> SelfRecipeLoader::describeSelf(const Env& env,
                                                      std::string& error) const
{
    try
    {
        auto input = BuildInput {};
        input.env = env;
        const auto described = describedRecipe(create, input);

        if (auto rejected = projectError(described->project()))
        {
            error = *rejected;
            return std::nullopt;
        }

        return described->project();
    }
    catch (const std::exception& thrown)
    {
        error = std::string {"the recipe threw: "} + thrown.what();
    }
    catch (...)
    {
        error = "the recipe threw";
    }

    return std::nullopt;
}

// The files RecipeManifestLoader keeps for a step, the command naming this
// executable, and the output written as the recipe's build command writes
// it; the step itself runs here. This process holds no package lock (see
// resolvePlan): the step takes it itself, in Package::cached.
std::optional<BuildOutput> SelfRecipeLoader::runSelfStep(const FilePath& directory,
                                                         const BuildInput& input,
                                                         std::string& error) const
{
    const auto files = writeStepFiles(
        directory, input, Files::executablePath(), recipe.parentDirectory(), error);

    if (!files)
        return std::nullopt;

    Log::step("running the build step of " + input.request.name
              + " in this process, as " + files->commandLine);

    auto output = runRecipeStep(create, input);

    if (!saveJsonFile(files->output, output))
    {
        error = "cannot write " + files->output.str();
        return std::nullopt;
    }

    return output;
}
} // namespace epm
