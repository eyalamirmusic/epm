#pragma once

#include <epm/Cache/CacheRoot.h>
#include <epm/Recipe/BuildProtocol.h>
#include <epm/Recipe/Project.h>
#include <epm/Spec/Env.h>

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>

namespace epm
{
// The manifest of a project: the recipe beside its top-level CMakeLists.txt.
inline constexpr auto manifestFileName = "epm.cpp";

// Reads the Project a directory's epm.cpp describes, and runs build steps in
// a recipe's executable. The graph walk gets one, as it gets a
// SourceFetcher: RecipeManifestLoader in the binary, a map in tests.
class ManifestLoader
{
public:
    virtual ~ManifestLoader() = default;

    // The project <directory>/epm.cpp describes for `env`, or an empty one
    // when the directory has no epm.cpp. nullopt, with `error` set, when the
    // recipe cannot be compiled or run.
    virtual std::optional<Project> load(const eacp::FilePath& directory,
                                        const Env& env,
                                        std::string& error) = 0;

    // Whether <directory> has an epm.cpp.
    virtual bool hasManifest(const eacp::FilePath& directory) const;

    // Whether build() can run: false where there are no recipe executables
    // (Windows, until RecipeBuild has an MSVC argument shape), and every step
    // then runs in epm's process (buildInProcess).
    virtual bool runsSteps() const = 0;

    // Runs the build step of input.request.name in the executable of the
    // recipe file `recipe`: `build --package <name>`. A step that failed
    // comes back as an output holding its error; nullopt, with `error` set,
    // when the recipe cannot be compiled or the step wrote no output.
    virtual std::optional<BuildOutput> build(const eacp::FilePath& recipe,
                                             const BuildInput& input,
                                             std::string& error) = 0;
};

// Compiles each epm.cpp into an executable under <cache>/recipe/
// (buildRecipe), runs it for `env` (runRecipe), and runs build steps in it
// with their input and output in `stepsDirectory` (epm configure's
// <out>/steps/): <name>.in.json and <name>.out.json, overwritten on every
// run and kept, so that a step can be run again by hand, under a debugger.
// <name>.command beside them holds that run, shell-quoted, absolute paths:
//
//     # run from: <the recipe's directory>
//     <recipe executable> build --package <name> --in <steps>/<name>.in.json
//         --out <steps>/<name>.out.json      (one line)
//
// Without a steps directory they go to a scratch directory of their own.
class RecipeManifestLoader final : public ManifestLoader
{
public:
    explicit RecipeManifestLoader(CacheRoot cacheToUse,
                                  eacp::FilePath stepsDirectoryToUse = {});

    std::optional<Project> load(const eacp::FilePath& directory,
                                const Env& env,
                                std::string& error) override;

    // As load, for a recipe file of any name; a missing one is an error.
    std::optional<Project>
        loadFile(const eacp::FilePath& recipe, const Env& env, std::string& error);

    bool runsSteps() const override;

    // The step's output reaches stdout as it runs. The step runs with
    // --quiet, as epm reports its failure, so a failure it records is printed
    // once, by epm; one it cannot record (it crashed, or wrote no output)
    // comes back as `error`, ending with the last lines of what it printed.
    // The kept <name>.command omits --quiet.
    std::optional<BuildOutput> build(const eacp::FilePath& recipe,
                                     const BuildInput& input,
                                     std::string& error) override;

private:
    std::optional<BuildOutput> runStep(const eacp::FilePath& executable,
                                       const eacp::FilePath& recipe,
                                       const eacp::FilePath& directory,
                                       const BuildInput& input,
                                       std::string& error) const;

    CacheRoot cache;
    eacp::FilePath stepsDirectory;
};
} // namespace epm
