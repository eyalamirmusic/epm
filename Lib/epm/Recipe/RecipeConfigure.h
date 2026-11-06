#pragma once

#include "RecipeMain.h"

#include <epm/Cache/CacheRoot.h>
#include <epm/Graph/Graph.h>
#include <epm/Graph/ManifestLoader.h>
#include <epm/Recipe/BuildProtocol.h>

#include <eacp/Core/Utils/FilePath.h>

#include <memory>
#include <optional>
#include <string>

// What a recipe executable runs in its own process: describe(), a build step,
// and `configure`, which drives the whole graph (Docs/Graph.md §6). epm
// configure runs the same configure, in the root recipe's executable
// (Apps/epm/Main.cpp), or in its own process where there are none.
namespace epm
{
// describe() of the Recipe `create` makes, with `input` as its context, so
// env() is input.env.
std::unique_ptr<Recipe> describedRecipe(RecipeFactory create,
                                        const BuildInput& input);

// The build step of input.request.name, in this process: the Recipe's own
// steps when it is input.recipeName, else those add<T> registered, else the
// default CMakePackage (an error when input.customBuild says it has custom
// steps). What describe() or the step throws is the output's error.
BuildOutput runRecipeStep(RecipeFactory create, const BuildInput& input);

// epm configure's arguments, and the recipe executable's configure.
struct ConfigureArgs
{
    eacp::FilePath manifest;
    eacp::FilePath env;
    eacp::FilePath overrides;

    // Where graph.json, graph.cmake and steps/ go.
    eacp::FilePath out;

    // The cache root; $EPM_CACHE (CacheRoot::fromEnvironment) when empty.
    eacp::FilePath cache;

    // The cache stores (--cache-store, repeated), in order;
    // $EPM_CACHE_STORES (CacheRoot::storesFromEnvironment) when empty.
    eacp::Vector<eacp::FilePath> stores;
    bool verbose = false;
};

// `args` with its paths made absolute against the working directory, so that
// they keep meaning what the caller meant once enterManifestDirectory moves.
ConfigureArgs absoluteConfigureArgs(ConfigureArgs args);

// Makes the directory of `manifest` the working directory, where describe()
// and the steps the root owns run, as every other recipe runs in its own:
// a recipe executable may be started anywhere (an IDE starts it in the build
// directory). false, with `error` set, when the manifest is missing or the
// directory cannot be entered.
bool enterManifestDirectory(const eacp::FilePath& manifest, std::string& error);

// The cache root of `args`, created, with its symlinks resolved: one spelling
// for every path written into the cache and matched in export files; and its
// cache stores, absolute.
CacheRoot configureCache(const ConfigureArgs& args);

// <out>/steps, absolute: a step runs in its recipe's directory.
eacp::FilePath configureStepsDirectory(const ConfigureArgs& args);

// The graph's inputs but the root project: the env and overrides files read,
// the process environment to fill the overrides. nullopt, with `error` set,
// when the manifest is missing or a file cannot be read.
std::optional<GraphInputs> configureInputs(const ConfigureArgs& args,
                                           std::string& error);

struct ConfigureResult
{
    bool ok = false;
    std::string error;

    // Child build output worth showing above the error line.
    std::string details;
};

// epm configure: describes the root through `loader` (loadFile), then
// configureGraph, which runs every recipe and build step through it, and
// writes graph.json and graph.cmake into args.out.
ConfigureResult runConfigure(const ConfigureArgs& args, ManifestLoader& loader);

// The loader of a recipe executable's configure: its own recipe, `recipe`
// (the --manifest), is described and runs its steps in this process; every
// other recipe goes through a RecipeManifestLoader, compiled into the cache
// and run as a process of its own. Its steps keep their files in
// `stepsDirectory` as that loader's do (writeStepFiles), <name>.command
// naming this executable, so a step can be run again by hand.
class SelfRecipeLoader final : public ManifestLoader
{
public:
    SelfRecipeLoader(RecipeFactory createToUse,
                     const eacp::FilePath& recipeToUse,
                     CacheRoot cache,
                     eacp::FilePath stepsDirectoryToUse = {});

    std::optional<Project> load(const eacp::FilePath& directory,
                                const Env& env,
                                std::string& error) override;

    std::optional<Project> loadFile(const eacp::FilePath& recipe,
                                    const Env& env,
                                    std::string& error) override;

    std::optional<BuildOutput> build(const eacp::FilePath& recipe,
                                     const BuildInput& input,
                                     std::string& error) override;

private:
    bool isSelf(const eacp::FilePath& recipe) const;
    std::optional<Project> describeSelf(const Env& env, std::string& error) const;

    std::optional<BuildOutput> runSelfStep(const eacp::FilePath& directory,
                                           const BuildInput& input,
                                           std::string& error) const;

    RecipeFactory create;
    eacp::FilePath recipe;
    eacp::FilePath stepsDirectory;
    RecipeManifestLoader others;
};
} // namespace epm
