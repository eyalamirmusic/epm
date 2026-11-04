# write_recipe(<file> <statements>) writes an epm.cpp whose describe() runs
# <statements>, C++ with `project` in scope.
function(write_recipe file statements)
    file(WRITE "${file}" "#include <epm/Recipe/Recipe.h>

using namespace epm;

namespace
{
struct TestRecipe final : Recipe
{
    void describe(Project& project, const Env&) override
    {
        ${statements}
    }
};
} // namespace

EPM_RECIPE(TestRecipe)
")
endfunction()
