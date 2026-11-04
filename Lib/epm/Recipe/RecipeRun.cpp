#include "RecipeRun.h"

#include <epm/Spec/JsonFile.h>
#include <epm/Util/Command.h>
#include <epm/Util/Log.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

namespace epm
{
using namespace eacp;

namespace
{
constexpr auto failureLines = 20;

RecipeRun runFailure(std::string error, std::string_view output = {})
{
    auto run = RecipeRun {};
    run.error = std::move(error);

    if (const auto tail = lastLines(output, failureLines); !tail.empty())
        run.error += "\n" + tail;

    return run;
}
} // namespace

RecipeRun runRecipe(const FilePath& executableToRun,
                    const Env& env,
                    const FilePath& workingDirectory)
{
    // Absolute, or a bare name would be looked up on PATH.
    const auto executable = resolvedPath(executableToRun);

    if (!Files::isFile(executable))
        return runFailure("no recipe executable at " + executable.str());

    // Its own directory per run: epm processes may run one recipe at once for
    // different envs.
    const auto scratch = TemporaryFile {};
    const auto envFile = scratch.path() / "env.json";
    const auto projectFile = scratch.path() / "project.json";

    if (!saveJsonFile(envFile, env))
        return runFailure("cannot write " + envFile.str());

    const auto arguments =
        Vector<std::string> {"--env", envFile.str(), "--out", projectFile.str()};
    Log::step("running " + executable.str() + " " + joinList(arguments, " "));

    const auto result = runCommand(executable.str(), arguments, workingDirectory);

    if (!result.ok)
        return runFailure("the recipe failed: "
                              + failureReason(executable.str(), result),
                          result.output);

    auto project = loadJsonFile<Project>(projectFile);

    if (!project)
        return runFailure("the recipe " + executable.str()
                              + " exited without writing its project",
                          result.output);

    auto run = RecipeRun {};
    run.project = std::move(*project);
    return run;
}
} // namespace epm
