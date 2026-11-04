#pragma once

#include <memory>

namespace epm
{
class Recipe;

// Makes the Recipe a recipe executable runs; EPM_RECIPE writes one.
using RecipeFactory = std::unique_ptr<Recipe> (*)();

// The main() of a recipe executable, which EPM_RECIPE writes:
//
//     epm-recipe [describe] [--env <env.json>] [--out <project.json>]
//     epm-recipe build --package <name> --in <input.json> --out <output.json>
//                      [--quiet]
//
// describe (the default): reads the Env (an empty one without --env), runs
// describe() on the Recipe `create` makes, and writes the Project as JSON to
// --out, or to stdout without it. A problem (an unreadable env, a recipe
// that throws, a project that projectError rejects) goes to stderr with exit
// code 1 and nothing is written.
//
// build: reads the BuildInput (Recipe/BuildProtocol.h), runs describe() for
// its env, then the build() of the package named: the Recipe itself when it
// is the input's recipeName, else the steps add<T> registered for it, else
// the default CMakePackage (an error when the input says the package has
// custom steps). Writes the BuildOutput to --out, a failure included, with
// exit code 1 when the step failed; then also prints the error on stderr
// (its details above it) unless --quiet, which epm passes because it reports
// the failure itself. An input it cannot read, or of another schema, is only
// reported on stderr. Progress and child build output go to stdout as the
// step runs.
int recipeMain(int argc, char** argv, RecipeFactory create);
} // namespace epm
