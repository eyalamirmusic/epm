#pragma once

#include "Project.h"

#include <epm/Spec/Env.h>

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>

namespace epm
{
struct RecipeRun
{
    std::optional<Project> project;

    // One line, then the end of what the recipe printed, when it failed.
    std::string error;
};

// Runs a recipe executable (buildRecipe(), or any program running recipeMain) as
// `<executable> describe --env <env.json> --out <project.json>`, both files in a
// scratch directory of their own, and reads the Project back. The recipe runs
// in `workingDirectory`, its epm.cpp's directory, so a file it reads relative
// to itself is its own; in this process's working directory when that is
// empty. Fails when it cannot be run, exits non-zero or is killed, or writes
// no readable project.
RecipeRun runRecipe(const eacp::FilePath& executable,
                    const Env& env,
                    const eacp::FilePath& workingDirectory = {});
} // namespace epm
