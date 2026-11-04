#pragma once

#include <memory>
#include <type_traits>

namespace epm
{
class Recipe;

// Makes the Recipe a recipe executable runs.
using RecipeFactory = std::unique_ptr<Recipe> (*)();

// What a recipe executable's main() returns, the default command line of a
// recipe (an epm.cpp's own main() may handle commands of its own first):
//
//     epm-recipe [configure] [--manifest <epm.cpp>] --env <env.json>
//                --overrides <overrides.json> --out <dir> [--cache <dir>]
//                [--verbose]
//     epm-recipe describe [--env <env.json>] [--out <project.json>]
//     epm-recipe build --package <name> --in <input.json> --out <output.json>
//                      [--quiet]
//
// configure (the default): what epm configure does, which runs it in its
// own build of the root recipe (Recipe/RecipeConfigure.h): describes this
// recipe and runs the build steps it owns in this process, every other
// recipe in an executable of its own, and writes graph.json, graph.cmake and
// steps/ into --out. --manifest is this recipe's file, epm.cpp in the working
// directory by default. The other paths are taken against the working
// directory it starts in, then it moves to the manifest's directory, where
// describe() and its steps run. A failure goes to stderr ("epm: <error>", its
// details above it), with exit code 1. Without arguments, or with an unknown
// command, it prints the usage with exit code 2.
//
// describe: reads the Env (an empty one without --env), runs
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

// recipeMain for the Recipe `RecipeType`, what an epm.cpp's main() returns:
//
//     int main(int argc, char** argv)
//     {
//         return epm::recipeMain<App>(argc, argv);
//     }
template <class RecipeType>
int recipeMain(int argc, char** argv)
{
    static_assert(std::is_base_of_v<Recipe, RecipeType>,
                  "recipeMain<T>: T must derive from epm::Recipe");

    return recipeMain(argc,
                      argv,
                      []() -> std::unique_ptr<Recipe>
                      { return std::make_unique<RecipeType>(); });
}
} // namespace epm
