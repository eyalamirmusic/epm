// HelloGraph's dependencies. epm compiles and runs this file itself at
// configure time; epm_add_recipe in CMakeLists.txt also builds it, so that the
// IDE and the build check it and a Debug build can step through describe().

#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
// How eacp is built into the cache: the default CMake flow, without its tests
// and examples. epm runs it in this file's executable; to debug it, run
// HelloGraphRecipe with
// `build --package eacp --in <build dir>/epm/steps/eacp.in.json --out <file>`.
struct Eacp final : CMakePackage
{
    void configure(CMakeBuild& build) override
    {
        build.option("EACP_ENABLE_TESTS", "OFF");
        build.option("EACP_ENABLE_EXAMPLES", "OFF");
    }
};

struct HelloGraph final : Recipe
{
    void describe() override
    {
        auto eacp = add<Eacp>("eacp", "eyalamirmusic/eacp", "main");

        for (auto option: {"EACP_BUILD_GRAPHICS", "EACP_BUILD_WEBVIEW"})
            eacp.option(option, "OFF");

        eacp.config("Release");

        // -DEPM_PROFILE=debug-deps also caches eacp's Debug build, to step
        // into it from a debugger.
        if (env().profile == "debug-deps")
            eacp.config("Debug");
    }
};
} // namespace

EPM_RECIPE(HelloGraph)
