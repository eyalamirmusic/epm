// The recipe RecipeTests compiles and loads. It is not part of any target:
// epm compiles it itself. FilePath's constructor, operator/ and str() live in
// eacp-core, so loading it proves the module binds to the test executable.

#include <epm/Recipe/Recipe.h>

#include <eacp/Core/Utils/FilePath.h>

using namespace epm;

namespace
{
struct TestProject final : Recipe
{
    void describe(Project& project, const Env& env) override
    {
        setOption(project.add("fmt", "fmtlib/fmt", "11.0.2"), "FMT_DOC", "OFF");

        if (env.systemName == "Linux")
        {
            const auto checkout = eacp::FilePath {"/opt/epm"} / "libuuid";
            addConfig(setMode(project.add("uuid", checkout.str()), "source"),
                      "Release");
        }
    }
};
} // namespace

EPM_RECIPE(TestProject)
