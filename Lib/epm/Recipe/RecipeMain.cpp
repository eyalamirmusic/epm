#include "RecipeMain.h"
#include "Recipe.h"
#include "RecipeConfigure.h"

#include <epm/Spec/JsonFile.h>
#include <epm/Util/Log.h>

#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Console.h>

#include <exception>
#include <memory>
#include <string>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto usage =
    "usage: epm-recipe [configure|describe|build] [options]\n"
    "\n"
    "commands:\n"
    "  configure  resolve this recipe's graph as epm configure does (the default)\n"
    "  describe   print the project describe() declares, as JSON\n"
    "  build      run the build step of one package, for epm\n"
    "\n"
    "<command> --help lists its options.";

int fail(const std::string& message)
{
    Console::printError("epm-recipe: " + message);
    return 1;
}

// The arguments after the first `skip`.
Vector<std::string> argumentsAfter(int argc, char** argv, int skip)
{
    auto arguments = Vector<std::string> {};

    for (auto i = skip; i < argc; ++i)
        arguments.add(argv[i]);

    return arguments;
}

// Runs `body`, turning what it throws into the one-line failure.
template <typename Body>
int guarded(Body&& body)
{
    try
    {
        return body();
    }
    catch (const std::exception& error)
    {
        return fail(std::string {"the recipe threw: "} + error.what());
    }
    catch (...)
    {
        return fail("the recipe threw");
    }
}

int describe(const Vector<std::string>& arguments, RecipeFactory create)
{
    auto envFile = FilePath {};
    auto outFile = FilePath {};

    auto cli = Args::Parser {"epm-recipe describe",
                             "Prints the project this epm.cpp describes, as JSON."};
    cli.option("--env", envFile, "the env JSON (default: an empty env)");
    cli.option("--out", outFile, "the project JSON to write (default: stdout)");

    if (auto code = cli.exitCodeFor(cli.parse(arguments)))
        return *code;

    return guarded(
        [&]
        {
            auto input = BuildInput {};

            if (!envFile.empty())
            {
                if (auto loaded = loadJsonFile<Env>(envFile))
                    input.env = std::move(*loaded);
                else
                    return fail("cannot read the env file " + envFile.str());
            }

            const auto recipe = describedRecipe(create, input);
            const auto& project = recipe->project();

            if (auto error = projectError(project))
                return fail(*error);

            if (outFile.empty())
                Console::print(Miro::toJSONString(project, 2));
            else if (!saveJsonFile(outFile, project))
                return fail("cannot write " + outFile.str());

            return 0;
        });
}

int build(const Vector<std::string>& arguments, RecipeFactory create)
{
    auto package = std::string {};
    auto inFile = FilePath {};
    auto outFile = FilePath {};
    auto quiet = false;

    auto cli = Args::Parser {"epm-recipe build",
                             "Runs the build step of one package, for epm."};
    cli.option("--package", package, "the package to build").required();
    cli.option("--in", inFile, "the build input JSON").required();
    cli.option("--out", outFile, "the build output JSON to write").required();
    cli.flag("--quiet",
             quiet,
             "print no failure on stderr: the caller reports it from --out");

    if (auto code = cli.exitCodeFor(cli.parse(arguments)))
        return *code;

    return guarded(
        [&]
        {
            const auto schema = buildProtocolSchemaOf(inFile);

            if (!schema)
                return fail("cannot read the build input " + inFile.str());

            if (*schema != buildProtocolSchema)
                return fail("the build input has schema " + std::to_string(*schema)
                            + ", but this recipe reads schema "
                            + std::to_string(buildProtocolSchema));

            const auto input = loadJsonFile<BuildInput>(inFile);

            if (!input)
                return fail("cannot read the build input " + inFile.str());

            if (input->request.name != package)
                return fail("--package " + package + ", but the build input is for "
                            + input->request.name);

            Log::setVerbose(input->verbose);

            const auto output = runRecipeStep(create, *input);

            if (!saveJsonFile(outFile, output))
                return fail("cannot write " + outFile.str());

            if (output.ok())
                return 0;

            if (quiet)
                return 1;

            if (!output.details.empty())
                Console::printError(output.details);

            return fail(output.error);
        });
}

// epm configure, in this process for this recipe and the steps it owns. Its
// failure reads as epm's own ("epm: ..."): epm configure passes this stderr
// on as its own.
int configure(const Vector<std::string>& arguments, RecipeFactory create)
{
    auto args = ConfigureArgs {};
    args.manifest = FilePath {manifestFileName};

    auto cli = Args::Parser {"epm-recipe configure",
                             "Resolves the dependency graph of this epm.cpp, as "
                             "epm configure does, running its own build steps in "
                             "this process, in the manifest's directory. The "
                             "default command; the others are describe and "
                             "build."};
    cli.option("--manifest",
               args.manifest,
               "this recipe's epm.cpp (default: epm.cpp in the working "
               "directory)");
    cli.option("--env", args.env, "the env JSON").required();
    cli.option("--overrides", args.overrides, "the overrides JSON").required();
    cli.option("--out", args.out, "the directory for graph.cmake and graph.json")
        .required();
    cli.option("--cache", args.cache, "the cache root (default: $EPM_CACHE)");
    cli.option("--cache-store",
               args.stores,
               "a read-only cache store missing binaries are copied from, "
               "searched in the order given (default: $EPM_CACHE_STORES)");
    cli.flag("--verbose", args.verbose, "log each step");

    if (auto code = cli.exitCodeFor(cli.parse(arguments)))
        return *code;

    Log::setVerbose(args.verbose);

    const auto configureFailed = [](const std::string& error)
    {
        Console::printError("epm: " + error);
        return 1;
    };

    return guarded(
        [&]
        {
            args = absoluteConfigureArgs(args);

            if (auto error = std::string {};
                !enterManifestDirectory(args.manifest, error))
                return configureFailed(error);

            auto loader = SelfRecipeLoader {create,
                                            args.manifest,
                                            configureCache(args),
                                            configureStepsDirectory(args)};
            const auto result = runConfigure(args, loader);

            if (result.ok)
                return 0;

            if (!result.details.empty())
                Console::printError(result.details);

            return configureFailed(result.error);
        });
}
} // namespace

int recipeMain(int argc, char** argv, RecipeFactory create)
{
    const auto command = argc > 1 ? std::string {argv[1]} : std::string {};

    const auto isCommand =
        command == "build" || command == "describe" || command == "configure";

    // Without arguments configure could only complain of a missing --env; a
    // typo of a command must not configure.
    if (!isCommand && !command.starts_with("-"))
    {
        if (!command.empty())
            Console::printError("epm-recipe: unknown command " + command);

        Console::printError(usage);
        return Args::badArguments;
    }

    if (command == "build")
        return build(argumentsAfter(argc, argv, 2), create);

    if (command == "describe")
        return describe(argumentsAfter(argc, argv, 2), create);

    if (command == "configure")
        return configure(argumentsAfter(argc, argv, 2), create);

    return configure(argumentsAfter(argc, argv, 1), create);
}
} // namespace epm
