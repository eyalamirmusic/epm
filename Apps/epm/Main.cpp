#include <epm/Cache/CacheListing.h>
#include <epm/Cache/CacheRoot.h>
#include <epm/Emit/GraphResponse.h>
#include <epm/Emit/RecipeSdkCMake.h>
#include <epm/Lock/LockFile.h>
#include <epm/Recipe/RecipeBuild.h>
#include <epm/Recipe/RecipeLoader.h>
#include <epm/Resolve/Commands.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Spec/PackageArguments.h>
#include <epm/Util/Log.h>

#include <eacp/Core/Utils/Environment.h>

#include "Version.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <algorithm>
#include <filesystem>

using namespace eacp;

namespace
{
constexpr auto usage =
    "usage: epm <command>\n"
    "\n"
    "commands:\n"
    "  configure    resolve an epm.cpp graph through the lock for\n"
    "               get_epm.cmake\n"
    "  graph-cmake  write graph.cmake from a graph.json\n"
    "  describe     print the project an epm.cpp describes\n"
    "  recipe-sdk   write how epm compiles an epm.cpp, for CMake\n"
    "  resolve      resolve one package for epm.cmake; its CMake arguments\n"
    "               follow --\n"
    "  provide      the same, for FetchContent_MakeAvailable\n"
    "  find         the same, for find_package of a name outside the graph\n"
    "  lock begin   start a configure: record the overrides, note a new lock\n"
    "  lock update  re-resolve the named packages' refs (all when none are\n"
    "               named) and rewrite the lock\n"
    "  lock verify  check each locked package's cache entry still hashes to\n"
    "               its lock hash (--full rehashes every tree), fetching a\n"
    "               package the cache does not have\n"
    "  lock root    print the root hash over every package in the lock\n"
    "  cache path   print the cache root\n"
    "  cache ls     list the cached binaries, sources and tools\n"
    "  --version    print the version";

constexpr auto argumentSeparator = std::string_view {"--"};

EA::Vector<std::string> argumentsAfter(int count)
{
    const auto& all = Apps::commandLineArgs();
    auto rest = EA::Vector<std::string> {};

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

FilePath getCacheDirectory(const FilePath& requested)
{
    const auto cache = requested.empty() ? epm::CacheRoot::fromEnvironment()
                                         : epm::CacheRoot {requested};
    return canonical(cache).path();
}

// epm-lock.json in the top-level source directory, else the current directory.
FilePath getDefaultLockFile(const FilePath& sourceDirectory)
{
    if (!sourceDirectory.empty())
        return sourceDirectory / epm::lockFileName;

    return FilePath::currentDirectory() / epm::lockFileName;
}

int runPackageCommand(const std::string& command, int argumentsToSkip)
{
    auto arguments = argumentsAfter(argumentsToSkip);
    auto options = epm::CommandOptions {};
    options.command = command;

    auto separator =
        std::find(arguments.begin(), arguments.end(), argumentSeparator);

    if (separator != arguments.end())
    {
        options.packageArguments.assign(separator + 1, arguments.end());
        arguments.getVector().erase(separator, arguments.end());
    }

    auto ciValue = std::string {};
    auto envFile = FilePath {};
    auto verbose = false;
    auto buildMissingValue = std::string {"ON"};
    auto parser = Args::Parser {"epm " + command, "Resolves packages for CMake."};

    if (command == "update")
        parser.positional(options.names, "name", "the packages to re-resolve");

    parser.option(
        "--mode", options.mode, "locked, update or offline [default: locked]");
    parser.option(
        "--ci", ciValue, "ON refuses <Name>_SOURCE overrides in locked mode");
    parser.option("--lock",
                  options.lock,
                  "the lock file [default: <source-dir>/epm-lock.json]");
    parser.option("--manifest",
                  options.manifest,
                  "the root project's epm.cpp, whose overrides win");
    parser.option("--overrides",
                  options.overridesFile,
                  "configure: the overrides.json epm.cmake wrote");
    parser.option("--out",
                  options.outputDirectory,
                  "configure: the directory for graph.cmake and graph.json");
    auto configsValue = std::string {};
    auto graphPackagesValue = std::string {};
    parser.option("--configs",
                  configsValue,
                  "the configs to build and cache, comma-separated, as epm.cmake "
                  "resolved them");
    parser.option("--graph-packages",
                  graphPackagesValue,
                  "the epm.cpp graph's packages (;-separated), which a package "
                  "call loads from the graph");
    parser.option("--abi-match",
                  options.abiMatch,
                  "which cached build may be used: compatible, strict or any");
    parser.option("--root-name",
                  options.rootName,
                  "who the top-level project's requirements are recorded as");
    parser.option("--cache", options.cache, "the cache root (default: $EPM_CACHE)");
    parser.option("--cmake", options.cmake, "the cmake that extracts tar archives");
    parser.option("--env", envFile, "the env JSON epm.cmake wrote");
    parser.option(
        "--source-dir", options.sourceDirectory, "the top-level source directory");
    parser.option(
        "--build-dir", options.buildDirectory, "the top-level build directory");
    parser.option(
        "--requested-by", options.requestedBy, "the directory asking for a package");
    parser.option("--requested-by-file",
                  options.requestedByFile,
                  "the CMake file asking for a package");
    parser.option("--requested-binary-dir",
                  options.requestedBinaryDirectory,
                  "its binary directory, which a relative SOURCE_DIR is under");
    parser.option("--output", options.output, "the CMake file to write");
    parser.option("--default-mode",
                  options.defaultMode,
                  "MODE when the call names none [default: auto]");
    parser.option("--build-missing-binaries",
                  buildMissingValue,
                  "OFF makes MODE auto fall back to source [default: ON]");
    parser.option("--override",
                  options.overrides,
                  "EPM_<Name>_SOURCE or CPM_<Name>_SOURCE=<dir>, for begin");
    parser.flag("--full",
                options.isFullVerify,
                "verify rehashes every tree instead of trusting stamps");
    parser.flag(
        "--announce", options.shouldAnnounce, "root prints '-- epm: root <hash>'");
    parser.flag("--verbose", verbose, "log each step");

    if (auto code = parser.exitCodeFor(parser.parse(arguments)))
        return *code;

    epm::Log::setVerbose(verbose);
    options.isCI = epm::isCMakeTrue(ciValue);

    for (const auto& config: Strings::split(configsValue, ','))
    {
        if (!config.empty())
            options.configs.add(config);
    }

    for (const auto& name: Strings::split(graphPackagesValue, ';'))
    {
        if (!name.empty())
            options.graphPackages.add(name);
    }
    options.buildMissingBinaries = !epm::isCMakeFalse(buildMissingValue);
    options.cache = getCacheDirectory(options.cache);

    try
    {
        if (!envFile.empty())
        {
            const auto env = epm::loadJsonFile<epm::Env>(envFile);

            if (!env)
                return fail("cannot read the env file " + envFile.str());

            options.env = *env;
        }

        if (options.lock.empty())
            options.lock = getDefaultLockFile(options.sourceDirectory);
        else if (!options.lock.isAbsolute())
            options.lock = FilePath::currentDirectory() / options.lock.str();

        if (command == "resolve")
            return epm::runResolveCommand(options);

        if (command == "configure")
            return epm::runConfigureCommand(options);

        if (command == "provide")
            return epm::runProvideCommand(options);

        if (command == "find")
            return epm::runFindCommand(options);

        if (command == "begin")
            return epm::runBeginCommand(options);

        if (command == "update")
            return epm::runUpdateCommand(options);

        if (command == "verify")
            return epm::runVerifyCommand(options);

        if (command == "root")
            return epm::runRootCommand(options);
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }

    Console::printError("epm: unknown command '" + command + "'\n\n" + usage);
    return Args::badArguments;
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
        const auto loaded = epm::loadJsonFile<epm::ResolvedGraph>(graph);

        if (!loaded)
            return fail("cannot read the graph file " + graph.str());

        Files::writeFileAtomically(out, epm::graphCMakeText(*loaded));
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

    try
    {
        epm::writeRecipeSdkCMake(out, epm::recipeSdk());
    }
    catch (const std::exception& error)
    {
        return fail("cannot write " + out.str() + ": " + error.what());
    }

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

    if (command == "resolve" || command == "provide" || command == "find"
        || command == "configure")
        return runPackageCommand(command, 2);

    if (command == "lock" && args.size() >= 3)
        return runPackageCommand(args[2], 3);

    if (command == "graph-cmake")
        return graphCMake();

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
