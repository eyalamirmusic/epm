// App's dependencies: Lib, which brings eacp pinned to source. App takes the
// binary cache back for eacp where it can, while Lib stays a source build.

#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
struct App final : Recipe
{
    void describe() override
    {
        // Where Lib would be published. CMakeLists.txt points EPM_Lib_SOURCE
        // at the sibling directory, so this is never fetched here.
        add("Lib", "example/Lib", "v1");

        // The root only adds to what Lib declares: no git, no tag. Mode is per
        // package, so a multi-config generator caches every config.
        if (env().isMultiConfig || env().buildType == "Release")
            add("eacp").mode("prebuilt");
    }
};
} // namespace

int main(int argc, char** argv)
{
    return recipeMain<App>(argc, argv);
}
