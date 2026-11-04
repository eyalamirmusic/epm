// HelloGraph's dependencies. epm compiles and runs this file itself at
// configure time; epm_add_recipe in CMakeLists.txt also builds it, so that the
// IDE and the build check it and a Debug build can step through describe().

#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
struct HelloGraph final : Recipe
{
    void describe(Project& project, const Env& env) override
    {
        auto& eacp = project.add("eacp", "eyalamirmusic/eacp", "main");

        for (auto option: {"EACP_BUILD_GRAPHICS",
                           "EACP_BUILD_WEBVIEW",
                           "EACP_ENABLE_TESTS",
                           "EACP_ENABLE_EXAMPLES"})
            setOption(eacp, option, "OFF");

        addConfig(eacp, "Release");

        // -DEPM_PROFILE=debug-deps also caches eacp's Debug build, to step
        // into it from a debugger.
        if (env.profile == "debug-deps")
            addConfig(eacp, "Debug");
    }
};
} // namespace

EPM_RECIPE(HelloGraph)
