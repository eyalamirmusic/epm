#pragma once

// The header an epm.cpp includes. epm compiles the file into an executable
// with the toolchain that built epm (RecipeBuild.h), linking the epm-recipe
// library, eacp-core, eacp-network and Miro, so a recipe may use anything
// those offer. epm runs it with an env.json and reads back the Project it
// describes as JSON (RecipeRun.h), and runs the build steps of the packages
// it declares through it (RecipeMain.h): the contract is JSON, not a C++ ABI.
//
//     #include <epm/Recipe/Recipe.h>
//
//     using namespace epm;
//
//     struct Fmt : CMakePackage
//     {
//         void configure(CMakeBuild& build) override
//         {
//             build.option("FMT_DOC", "OFF");
//         }
//     };
//
//     struct App : Recipe
//     {
//         void describe() override
//         {
//             auto fmt = add<Fmt>("fmt", "fmtlib/fmt", "11.0.2");
//             fmt.option("FMT_TEST", "OFF");
//             add("nlohmann_json", "nlohmann/json", "v3.11.3");
//
//             if (env().profile == "debug-deps")
//                 fmt.config("Debug");
//         }
//     };
//
//     int main(int argc, char** argv)
//     {
//         return recipeMain<App>(argc, argv);
//     }

#include "CMakePackage.h"
#include "Package.h"
#include "Project.h"
#include "RecipeMain.h"

#include <epm/Spec/Env.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace epm
{
// One package a Recipe declares, to set its fields in a chain:
// add("fmt", "fmtlib/fmt", "11.0.2").option("FMT_DOC", "OFF").mode("source").
// It stays valid across later add() calls.
class PackageDecl
{
public:
    PackageDecl(Project& projectToUse, int indexToUse);

    PackageDecl& option(std::string key, std::string value);
    PackageDecl& config(std::string config);
    PackageDecl& mode(std::string mode);
    PackageDecl& abiMatch(std::string abiMatch);

    ManifestPackage& package() const;

private:
    Project* project;
    int index;
};

// An epm.cpp's project. describe() declares the packages it depends on, for
// env(); the build() it inherits (CMakePackage) is how epm builds the project
// itself when it is another project's dependency.
class Recipe : public CMakePackage
{
public:
    virtual void describe() = 0;

    // Appends a dependency (Project::add) built by the default steps.
    PackageDecl add(std::string name, std::string git = {}, std::string tag = {});

    // Appends a dependency built by `T`'s steps (a Package), which epm runs
    // through this recipe: `epm-recipe build --package <name>`.
    template <typename T>
    PackageDecl add(std::string name, std::string git = {}, std::string tag = {})
    {
        static_assert(std::is_base_of_v<Package, T>,
                      "add<T>: T must derive from epm::Package");

        factories[name] = [] { return std::make_unique<T>(); };
        auto declared = add(std::move(name), std::move(git), std::move(tag));
        declared.package().customBuild = true;
        return declared;
    }

    // What describe() declared.
    const Project& project() const;

    // The steps add<T> registered for `name`; nullptr when it has none.
    std::unique_ptr<Package> createPackage(std::string_view name) const;

private:
    Project described;
    std::map<std::string, std::function<std::unique_ptr<Package>()>, std::less<>>
        factories;
};
} // namespace epm

// A shorthand, once per epm.cpp at global scope, for the main() returning
// epm::recipeMain<Type>(argc, argv), which is the documented form.
#define EPM_RECIPE(Type)                                                            \
    int main(int argc, char** argv)                                                 \
    {                                                                               \
        return epm::recipeMain<Type>(argc, argv);                                   \
    }
