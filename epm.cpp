// epm's own dependencies, which the released epm CMakeLists.txt pins resolves
// through epm-lock.json.

#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
struct Epm final : Recipe
{
    void describe(Project& project, const Env&) override
    {
        auto& eacp =
            setMode(project.add("eacp", "eyalamirmusic/eacp", "develop"), "source");

        for (auto option: {"EACP_BUILD_GRAPHICS",
                           "EACP_BUILD_WEBVIEW",
                           "EACP_ENABLE_TESTS",
                           "EACP_ENABLE_EXAMPLES"})
            setOption(eacp, option, "OFF");

        setMode(project.add("Miro", "eyalamirmusic/Miro", "main"), "source");
        setMode(project.add("NanoTest", "eyalamirmusic/NanoTest", "main"), "source");
    }
};
} // namespace

EPM_RECIPE(Epm)
