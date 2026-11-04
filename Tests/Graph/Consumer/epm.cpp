// What Tests/GraphProvider.cmake's consumer depends on. Only its presence
// matters there: the fake epm answers with a ready graph.cmake and never
// compiles it.

#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
struct Consumer final : Recipe
{
    void describe(Project& project, const Env&) override
    {
        setOption(project.add("Leaf", "example/Leaf", "v1.0.0"), "LEAF_FLAG", "ON");
        project.add("Fetched", "example/Fetched", "main");
    }
};
} // namespace

EPM_RECIPE(Consumer)
