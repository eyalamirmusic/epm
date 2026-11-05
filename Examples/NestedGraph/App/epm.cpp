// App's dependencies: Lib, which brings eacp, prebuilt, as Lib's epm.cpp
// declares it.

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
    }
};
} // namespace

int main(int argc, char** argv)
{
    return recipeMain<App>(argc, argv);
}
