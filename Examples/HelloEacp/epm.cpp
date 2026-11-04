// HelloEacp's recipe. eacp itself is added by epm_add_package in
// CMakeLists.txt; what eacp finds on the machine on Linux is named here, since
// a find_package of a name outside the graph never reaches CMake's own search
// otherwise.

#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
struct HelloEacp final : Recipe
{
    void describe(Project& project, const Env&) override
    {
        for (auto name: {"CURL", "PkgConfig", "Threads"})
            project.addSystem(name);
    }
};
} // namespace

EPM_RECIPE(HelloEacp)
