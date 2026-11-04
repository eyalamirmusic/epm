// Lib's dependencies: eacp, pinned to source, so a library developer builds
// and steps through the eacp it links. An app using Lib can take the binary
// cache back (App/epm.cpp).

#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
// How eacp is built into the cache: the default CMake flow, without its tests
// and examples. Lib owns these steps, so they run in Lib's recipe executable
// wherever Lib is used, an app taking eacp prebuilt included.
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
        auto eacp = add<Eacp>("eacp", "eyalamirmusic/eacp", "main").mode("source");

        for (auto option: {"EACP_BUILD_GRAPHICS", "EACP_BUILD_WEBVIEW"})
            eacp.option(option, "OFF");
    }
};
} // namespace

int main(int argc, char** argv)
{
    return recipeMain<Lib>(argc, argv);
}
