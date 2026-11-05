// Lib's dependencies: eacp, prebuilt from the binary cache, Release where a
// Debug build links it. An app using Lib (App/epm.cpp) gets the same eacp
// through this file.

#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
// How eacp is built into the cache: the default CMake flow, without its tests
// and examples. Lib owns these steps, so they run in Lib's recipe executable
// wherever Lib is used, in an app as much as here.
struct Eacp final : CMakePackage
{
    void configure(CMakeBuild& build) override
    {
        build.option("EACP_ENABLE_TESTS", "OFF");
        build.option("EACP_ENABLE_EXAMPLES", "OFF");
    }
};

struct Lib final : Recipe
{
    void describe() override
    {
        // Prebuilt (the default mode), and Release only where a Debug build
        // of Lib or of an app using it links Release archives: everywhere but
        // the MSVC ABI, which gets the build's own configuration instead.
        auto eacp =
            add<Eacp>("eacp", "eyalamirmusic/eacp", "main").releaseIfPossible();

        for (auto option: {"EACP_BUILD_GRAPHICS", "EACP_BUILD_WEBVIEW"})
            eacp.option(option, "OFF");
    }
};
} // namespace

int main(int argc, char** argv)
{
    return recipeMain<Lib>(argc, argv);
}
