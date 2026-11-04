#include "ManifestLoader.h"

#include <epm/Recipe/RecipeBuild.h>
#include <epm/Recipe/RecipeRun.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto failureLines = 20;

std::string withLastLines(std::string error, std::string_view output)
{
    if (const auto tail = lastLines(output, failureLines); !tail.empty())
        error += "\n" + tail;

    return error;
}

// `text` as one POSIX shell word: as it is when nothing in it is special,
// else single-quoted. (Upstream candidate for eacp's Strings.)
std::string shellQuoted(std::string_view text)
{
    const auto plain = [](char c)
    {
        return std::isalnum(static_cast<unsigned char>(c))
               || std::string_view {"@%+=:,./_-"}.find(c) != std::string_view::npos;
    };

    if (!text.empty() && std::ranges::all_of(text, plain))
        return std::string {text};

    auto quoted = std::string {"'"};

    for (const auto c: text)
        quoted += c == '\'' ? std::string {"'\\''"} : std::string {c};

    return quoted + "'";
}

std::string shellCommand(const FilePath& executable,
                         const Vector<std::string>& arguments)
{
    auto command = shellQuoted(executable.str());

    for (const auto& argument: arguments)
        command += " " + shellQuoted(argument);

    return command;
}

// <name>.command beside the step's input: where it runs from, then the
// command, for running the step again by hand. Nothing reads it back.
bool writeCommandFile(const FilePath& path,
                      const FilePath& workingDirectory,
                      const std::string& command)
{
    const auto text =
        "# run from: " + workingDirectory.str() + "\n" + command + "\n";

    try
    {
        Files::writeFile(
            path, {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }
    catch (const std::exception&)
    {
        return false;
    }

    return true;
}
} // namespace

std::optional<Project> ManifestLoader::loadFile(const FilePath& recipe,
                                                const Env& env,
                                                std::string& error)
{
    if (!Files::isFile(recipe))
    {
        error = "no recipe at " + recipe.str();
        return std::nullopt;
    }

    if (recipe.fileName() != manifestFileName)
    {
        error = "this loader reads only " + std::string {manifestFileName}
                + " files, not " + recipe.str();
        return std::nullopt;
    }

    return load(recipe.parentDirectory(), env, error);
}

bool ManifestLoader::hasManifest(const FilePath& directory) const
{
    return Files::isFile(directory / manifestFileName);
}

std::optional<StepFiles> writeStepFiles(const FilePath& directory,
                                        const BuildInput& input,
                                        const FilePath& executable,
                                        const FilePath& workingDirectory,
                                        std::string& error)
{
    const auto& name = input.request.name;
    const auto stepFiles = resolvedPath(directory);

    auto files = StepFiles {};
    files.input = stepFiles / (name + ".in.json");
    files.output = stepFiles / (name + ".out.json");
    files.command = stepFiles / (name + ".command");

    if (Files::exists(files.output) && !Files::removeFile(files.output))
    {
        error = "cannot remove " + files.output.str();
        return std::nullopt;
    }

    if (!saveJsonFile(files.input, input))
    {
        error = "cannot write " + files.input.str();
        return std::nullopt;
    }

    files.arguments = {"build",
                       "--package",
                       name,
                       "--in",
                       files.input.str(),
                       "--out",
                       files.output.str()};
    files.commandLine = shellCommand(resolvedPath(executable), files.arguments);

    if (!writeCommandFile(
            files.command, resolvedPath(workingDirectory), files.commandLine))
    {
        error = "cannot write " + files.command.str();
        return std::nullopt;
    }

    return files;
}

RecipeManifestLoader::RecipeManifestLoader(CacheRoot cacheToUse,
                                           FilePath stepsDirectoryToUse)
    : cache(std::move(cacheToUse))
    , stepsDirectory(std::move(stepsDirectoryToUse))
{
}

std::optional<Project> RecipeManifestLoader::load(const FilePath& directory,
                                                  const Env& env,
                                                  std::string& error)
{
    if (!hasManifest(directory))
        return Project {};

    return loadFile(directory / manifestFileName, env, error);
}

std::optional<Project> RecipeManifestLoader::loadFile(const FilePath& recipe,
                                                      const Env& env,
                                                      std::string& error)
{
    const auto build = buildRecipe(recipe, cache, FilePath {env.epmExecutable});

    if (!build.ok)
    {
        error = recipeBuildError(build);
        return std::nullopt;
    }

    Log::step((build.cached ? "cached " : "built ") + build.executable.str());

    auto run =
        runRecipe(build.executable, env, resolvedPath(recipe).parentDirectory());

    if (!run.project)
        error = "the recipe " + recipe.str() + ": " + run.error;

    return std::move(run.project);
}

bool RecipeManifestLoader::runsSteps() const
{
    return !Platform::isWindows();
}

std::optional<BuildOutput> RecipeManifestLoader::build(const FilePath& recipe,
                                                       const BuildInput& input,
                                                       std::string& error)
{
    const auto built =
        buildRecipe(recipe, cache, FilePath {input.env.epmExecutable});

    if (!built.ok)
    {
        error = "the recipe " + recipe.str() + ": " + recipeBuildError(built);
        return std::nullopt;
    }

    if (!stepsDirectory.empty())
        return runStep(built.executable, recipe, stepsDirectory, input, error);

    const auto scratch = TemporaryFile {};
    return runStep(built.executable, recipe, scratch.path(), input, error);
}

std::optional<BuildOutput> RecipeManifestLoader::runStep(const FilePath& executable,
                                                         const FilePath& recipe,
                                                         const FilePath& directory,
                                                         const BuildInput& input,
                                                         std::string& error) const
{
    const auto workingDirectory = resolvedPath(recipe).parentDirectory();
    const auto program = resolvedPath(executable);

    // The kept command has no --quiet, so a run by hand prints the failure.
    const auto files =
        writeStepFiles(directory, input, program, workingDirectory, error);

    if (!files)
        return std::nullopt;

    const auto& outFile = files->output;
    const auto& command = files->commandLine;

    // Its failure reaches the user once, from this process.
    auto quietArguments = files->arguments;
    quietArguments.add("--quiet");

    Log::step("running " + shellCommand(program, quietArguments));

    // Streamed as it runs: the step prints progress, and its child builds'
    // output under --verbose. The result travels through --out only.
    const auto result =
        runCommand(program.str(), quietArguments, workingDirectory, Echo::always);

    const auto schema = buildProtocolSchemaOf(outFile);

    if (schema && *schema != buildProtocolSchema)
    {
        error = command + " wrote schema " + std::to_string(*schema)
                + " output, but this epm reads schema "
                + std::to_string(buildProtocolSchema);
        return std::nullopt;
    }

    auto output = loadJsonFile<BuildOutput>(outFile);

    // The output was echoed as it came, so a failure the step reports
    // carries only its own details; one it could not report gets the last
    // lines, which hold whatever the executable printed on its way out.
    if (!output || (!result.ok && output->ok()))
    {
        const auto why = result.ok ? "it wrote no " + outFile.str()
                                   : failureReason(program.str(), result);
        error = withLastLines(command + " failed: " + why, result.output);
        return std::nullopt;
    }

    return output;
}
} // namespace epm
