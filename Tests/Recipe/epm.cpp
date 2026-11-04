// The recipe RecipeTests compiles and runs. It is not part of any target: epm
// compiles it itself. FilePath and Processes live in eacp-core, so running it
// proves a recipe executable links eacp-core as well as epm-recipe.

#include <epm/Recipe/Recipe.h>

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/FilePath.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

using namespace epm;

namespace
{
// Steps of its own for one package: the default CMake flow plus an option,
// which RecipeTests finds in the harvested export file.
struct Tiny final : CMakePackage
{
    void configure(CMakeBuild& build) override { build.option("TINY_VALUE", "42"); }
};

struct TestProject final : Recipe
{
    void describe() override
    {
        add("fmt", "fmtlib/fmt", "11.0.2").option("FMT_DOC", "OFF");

        if (env().systemName == "Linux")
        {
            const auto checkout = eacp::FilePath {"/opt/epm"} / "libuuid";
            add("uuid", checkout.str()).mode("source").config("Release");
        }

        // `epm-recipe build --package Tiny` runs Tiny's steps.
        if (env().profile == "build")
            add<Tiny>("Tiny");

        // A package named by what a child process prints.
        if (env().profile == "shell")
            add(eacp::Processes::run("/bin/sh", {"-c", "printf shell"}).output);

        // A package named after the directory the recipe runs in.
        if (env().profile == "cwd")
            add(std::filesystem::current_path().filename().string());

        if (env().profile == "throw")
            throw std::runtime_error {"asked to throw"};

        if (env().profile == "abort")
            std::abort();

        // Ends before the project is written.
        if (env().profile == "exit0")
            std::exit(0);
    }
};
} // namespace

EPM_RECIPE(TestProject)
