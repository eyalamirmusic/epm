// The recipe RecipeTests compiles and runs. It is not part of any target: epm
// compiles it itself. FilePath and Processes live in eacp-core, so running it
// proves a recipe executable links eacp-core as well as epm-recipe.

#include <epm/Recipe/Recipe.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Strings.h>

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

        // `epm-recipe build --package Tiny` runs Tiny's steps; a configure
        // fetches it from the repository RecipeTests made.
        if (env().profile == "build")
        {
            const auto git = eacp::getEnvValue("EPM_TEST_TINY_GIT");
            add<Tiny>("Tiny", git, git.empty() ? "" : "v1");
        }

        // A package named by what a child process prints.
        if (env().profile == "shell")
        {
            const auto printed =
                eacp::Platform::isWindows()
                    ? eacp::Processes::run("cmd.exe", {"/c", "echo shell"}).output
                    : eacp::Processes::run("/bin/sh", {"-c", "printf shell"}).output;
            add(eacp::Strings::trim(printed));
        }

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

int main(int argc, char** argv)
{
    return recipeMain<TestProject>(argc, argv);
}
