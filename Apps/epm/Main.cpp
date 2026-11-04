#include <epm/Cache/CacheListing.h>
#include <epm/Cache/CacheRoot.h>
#include <epm/Emit/GraphResponse.h>
#include <epm/Emit/RecipeSdkCMake.h>
#include <epm/Emit/Response.h>
#include <epm/Graph/Graph.h>
#include <epm/Recipe/RecipeBuild.h>
#include <epm/Recipe/RecipeLoader.h>
#include <epm/Resolve/Resolve.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Log.h>

#include <eacp/Core/Utils/Environment.h>

#include "Version.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>
#include <optional>

using namespace eacp;

namespace
{
constexpr auto usage =
    "usage: epm <command>\n"
    "\n"
    "commands:\n"
    "  configure    resolve an epm.cpp graph for get_epm.cmake\n"
    "  graph-cmake  write graph.cmake from a graph.json\n"
    "  describe     print the project an epm.cpp describes\n"
    "  recipe-sdk   write how epm compiles an epm.cpp, for CMake\n"
    "  resolve      resolve one package request for epm.cmake\n"
    "  cache path   print the cache root\n"
    "  cache ls     list the cached binaries, sources and tools\n"
    "  --version    print the version";

Vector<std::string> argumentsAfter(int count)
{
    const auto& all = Apps::commandLineArgs();
    auto rest = Vector<std::string> {};

    for (auto i = count; i < all.size(); ++i)
        rest.add(all[i]);

    return rest;
}

int fail(const std::string& message)
{
    Console::printError("epm: " + message);
    return 1;
}

// One spelling for every path epm writes into the cache and matches in export
// files, whatever symlinks (/tmp on macOS) the cache root was given through.
epm::CacheRoot canonical(const epm::CacheRoot& cache)
{
    Files::createDirectories(cache.path());
    auto failure = std::error_code {};
    const auto resolved =
        std::filesystem::weakly_canonical(toStdPath(cache.path()), failure);

    return failure ? cache : epm::CacheRoot {FilePath {resolved}};
}

int resolve()
{
    auto request = FilePath {};
    auto env = FilePath {};
    auto response = FilePath {};
    auto cache = FilePath {};
    auto verbose = false;

    auto cli = Args::Parser {"epm resolve", "Resolves one package request."};
    cli.option("--request", request, "the request JSON").required();
    cli.option("--env", env, "the env JSON").required();
    cli.option("--response", response, "the response CMake file to write")
        .required();
    cli.option("--cache", cache, "the cache root (default: $EPM_CACHE)");
    cli.flag("--verbose", verbose, "log each step");

    if (auto code = cli.exitCodeFor(cli.parse(argumentsAfter(2))))
        return *code;

    epm::Log::setVerbose(verbose);

    try
    {
        const auto envSpec = epm::loadJsonFile<epm::Env>(env);

        if (!envSpec)
            return fail("cannot read the env file " + env.str());

        const auto requestSpec = epm::loadJsonFile<epm::Request>(request);

        if (!requestSpec)
            return fail("cannot read the request file " + request.str());

        const auto cacheRoot = cache.empty() ? epm::CacheRoot::fromEnvironment()
                                             : epm::CacheRoot {cache};
        const auto result =
            epm::resolvePackage(*requestSpec, *envSpec, canonical(cacheRoot));

        if (!result.ok)
        {
            if (!result.details.empty())
                Console::printError(result.details);

            return fail(result.error);
        }

        if (!epm::writeResponse(response, result.response))
            return fail("cannot write " + response.str());
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }

    return 0;
}

int configure()
{
    auto manifest = FilePath {};
    auto env = FilePath {};
    auto overrides = FilePath {};
    auto out = FilePath {};
    auto cache = FilePath {};
    auto verbose = false;

    auto cli = Args::Parser {"epm configure",
                             "Resolves the dependency graph of an epm.cpp."};
    cli.option("--manifest", manifest, "the root epm.cpp").required();
    cli.option("--env", env, "the env JSON").required();
    cli.option("--overrides", overrides, "the overrides JSON").required();
    cli.option("--out", out, "the directory for graph.cmake and graph.json")
        .required();
    cli.option("--cache", cache, "the cache root (default: $EPM_CACHE)");
    cli.flag("--verbose", verbose, "log each step");

    if (auto code = cli.exitCodeFor(cli.parse(argumentsAfter(2))))
        return *code;

    epm::Log::setVerbose(verbose);

    try
    {
        auto inputs = epm::GraphInputs {};
        inputs.environment = getEnv;

        if (!Files::isFile(manifest))
            return fail("no manifest at " + manifest.str());

        if (auto loaded = epm::loadJsonFile<epm::Env>(env))
            inputs.env = std::move(*loaded);
        else
            return fail("cannot read the env file " + env.str());

        if (auto loaded = epm::loadJsonFile<epm::Overrides>(overrides))
            inputs.overrides = std::move(*loaded);
        else
            return fail("cannot read the overrides file " + overrides.str());

        const auto cacheRoot =
            canonical(cache.empty() ? epm::CacheRoot::fromEnvironment()
                                    : epm::CacheRoot {cache});
        auto loader = epm::RecipeManifestLoader {cacheRoot};
        auto loadError = std::string {};

        if (auto project = loader.loadFile(manifest, inputs.env, loadError))
            inputs.project = std::move(*project);
        else
            return fail("the manifest " + manifest.str() + ": " + loadError);

        const auto result = epm::configureGraph(inputs, cacheRoot, loader);

        if (!result.ok)
        {
            if (!result.details.empty())
                Console::printError(result.details);

            return fail(result.error);
        }

        if (!epm::writeGraphJson(out / "graph.json", result.graph))
            return fail("cannot write " + (out / "graph.json").str());

        if (!epm::writeGraphCMake(out / "graph.cmake", result.graph))
            return fail("cannot write " + (out / "graph.cmake").str());
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }

    return 0;
}

int graphCMake()
{
    auto graph = FilePath {};
    auto out = FilePath {};

    auto cli = Args::Parser {"epm graph-cmake",
                             "Writes graph.cmake from a resolved graph.json."};
    cli.option("--graph", graph, "the graph.json to read").required();
    cli.option("--out", out, "the graph.cmake to write").required();

    if (auto code = cli.exitCodeFor(cli.parse(argumentsAfter(2))))
        return *code;

    try
    {
        const auto loaded = epm::loadGraphJson(graph);

        if (!loaded)
            return fail("cannot read the graph file " + graph.str());

        if (!epm::writeGraphCMake(out, *loaded))
            return fail("cannot write " + out.str());
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }

    return 0;
}

// The module epm_add_recipe built: the project's compiler and flags, which
// need only be close enough to epm's for the Recipe ABI to match.
epm::RecipeLoad loadProjectModule(const FilePath& module, const FilePath& recipe)
{
    epm::Log::step("loading " + module.str()
                   + (recipe.empty() ? "" : " (built from " + recipe.str() + ")"));
    auto load = epm::loadRecipe(module);

    if (!load.recipe)
        load.error += "\n(was " + module.str()
                      + " built with another compiler than epm's, "
                      + epm::recipeSdk().compiler + "?)";

    return load;
}

std::optional<epm::RecipeLoad> loadCachedModule(const FilePath& recipe,
                                                const FilePath& cache)
{
    const auto cacheRoot =
        cache.empty() ? epm::CacheRoot::fromEnvironment() : epm::CacheRoot {cache};
    const auto build = epm::buildRecipe(recipe, canonical(cacheRoot));

    if (!build.ok)
    {
        if (!build.details.empty())
            Console::printError(build.details);

        fail(build.error);
        return std::nullopt;
    }

    epm::Log::step((build.cached ? "cached " : "built ") + build.module.str());
    return epm::loadRecipe(build.module);
}

int describe()
{
    auto recipe = FilePath {};
    auto module = FilePath {};
    auto env = FilePath {};
    auto cache = FilePath {};
    auto verbose = false;

    auto cli = Args::Parser {"epm describe",
                             "Compiles an epm.cpp (or takes a --module built from "
                             "one), loads it and prints the project it "
                             "describes."};
    cli.positional(recipe,
                   "epm.cpp",
                   "the recipe to describe (only informational with --module)");
    cli.option("--module",
               module,
               "load this module instead of compiling the epm.cpp, such as "
               "epm_add_recipe's Debug build, to debug it");
    cli.option("--env", env, "the env JSON (default: an empty env)");
    cli.option("--cache", cache, "the cache root (default: $EPM_CACHE)");
    cli.flag("--verbose", verbose, "log each step");

    if (auto code = cli.exitCodeFor(cli.parse(argumentsAfter(2))))
        return *code;

    if (recipe.empty() && module.empty())
        return cli.fail("give an epm.cpp, a --module, or both");

    if (!module.empty() && !Files::isFile(module))
        return fail("no recipe module at " + module.str()
                    + ": build its epm_add_recipe target first");

    epm::Log::setVerbose(verbose);

    try
    {
        auto envSpec = epm::Env {};

        if (!env.empty())
        {
            if (auto loaded = epm::loadJsonFile<epm::Env>(env))
                envSpec = std::move(*loaded);
            else
                return fail("cannot read the env file " + env.str());
        }

        auto load = module.empty() ? loadCachedModule(recipe, cache)
                                   : loadProjectModule(module, recipe);

        if (!load)
            return 1;

        if (!load->recipe)
            return fail(load->error);

        Console::print(Miro::toJSONString(load->recipe->describe(envSpec), 2));
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }

    return 0;
}

int recipeSdk()
{
    auto out = FilePath {};

    auto cli = Args::Parser {"epm recipe-sdk",
                             "Writes the compiler, standard, include directories, "
                             "definitions and options epm compiles an epm.cpp "
                             "with, as CMake variables (epm.cmake's epm::recipe)."};
    cli.option("--out", out, "the CMake file to write").required();

    if (auto code = cli.exitCodeFor(cli.parse(argumentsAfter(2))))
        return *code;

    if (!epm::writeRecipeSdkCMake(out, epm::recipeSdk()))
        return fail("cannot write " + out.str());

    return 0;
}

int cache()
{
    const auto rest = argumentsAfter(2);

    if (rest.size() == 1 && rest[0] == "path")
    {
        Console::print(epm::CacheRoot::fromEnvironment().path().str());
        return 0;
    }

    if (!rest.empty() && rest[0] == "ls")
    {
        auto root = FilePath {};
        auto cli = Args::Parser {"epm cache ls", "Lists what the cache holds."};
        cli.option("--cache", root, "the cache root (default: $EPM_CACHE)");

        if (auto code = cli.exitCodeFor(cli.parse(argumentsAfter(3))))
            return *code;

        const auto cacheRoot =
            root.empty() ? epm::CacheRoot::fromEnvironment() : epm::CacheRoot {root};
        Console::print(epm::cacheListing(canonical(cacheRoot)));
        return 0;
    }

    Console::printError(
        "usage: epm cache path\n       epm cache ls [--cache <dir>]");
    return Args::badArguments;
}

int run()
{
    const auto& args = Apps::commandLineArgs();

    if (args.size() < 2)
    {
        Console::printError(usage);
        return Args::badArguments;
    }

    const auto& command = args[1];

    if (command == "--version")
    {
        Console::print(epm::version);
        return 0;
    }

    if (command == "--help" || command == "-h")
    {
        Console::print(usage);
        return 0;
    }

    if (command == "configure")
        return configure();

    if (command == "graph-cmake")
        return graphCMake();

    if (command == "resolve")
        return resolve();

    if (command == "describe")
        return describe();

    if (command == "recipe-sdk")
        return recipeSdk();

    if (command == "cache")
        return cache();

    Console::printError("epm: unknown command '" + command + "'\n\n" + usage);
    return Args::badArguments;
}
} // namespace

int main(int argc, char* argv[])
{
    return Apps::runConsole(argc, argv, run);
}
