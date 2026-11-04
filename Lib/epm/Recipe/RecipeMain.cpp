#include "RecipeMain.h"
#include "Recipe.h"

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

std::unique_ptr<Recipe> describedRecipe(RecipeFactory create,
                                        const BuildInput& input)
{
    auto recipe = create();
    PackageRunner::setInput(*recipe, input);
    recipe->describe();
    return recipe;
}

int describe(const Vector<std::string>& arguments, RecipeFactory create)
{
    auto envFile = FilePath {};
    auto outFile = FilePath {};

    auto cli = Args::Parser {"epm-recipe",
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

// The step of input.request.name, what describe() throws included.
BuildOutput runStep(RecipeFactory create, const BuildInput& input)
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

            const auto output = runStep(create, *input);

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
} // namespace

int recipeMain(int argc, char** argv, RecipeFactory create)
{
    const auto command = argc > 1 ? std::string {argv[1]} : std::string {};

    if (command == "build")
        return build(argumentsAfter(argc, argv, 2), create);

    if (command == "describe")
        return describe(argumentsAfter(argc, argv, 2), create);

    return describe(argumentsAfter(argc, argv, 1), create);
}
} // namespace epm
