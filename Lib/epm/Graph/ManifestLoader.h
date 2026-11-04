#pragma once

#include <epm/Cache/CacheRoot.h>
#include <epm/Recipe/Project.h>
#include <epm/Spec/Env.h>

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>

namespace epm
{
// The manifest of a project: the recipe beside its top-level CMakeLists.txt.
inline constexpr auto manifestFileName = "epm.cpp";

// Reads the Project a directory's epm.cpp describes. The graph walk gets one,
// as it gets a SourceFetcher: RecipeManifestLoader in the binary, a map in
// tests.
class ManifestLoader
{
public:
    virtual ~ManifestLoader() = default;

    // The project <directory>/epm.cpp describes for `env`, or an empty one
    // when the directory has no epm.cpp. nullopt, with `error` set, when the
    // recipe cannot be compiled, loaded or run.
    virtual std::optional<Project> load(const eacp::FilePath& directory,
                                        const Env& env,
                                        std::string& error) = 0;
};

// Compiles each epm.cpp into <cache>/recipe/ (buildRecipe), loads it into
// this process (loadRecipe) and calls its describe(env).
class RecipeManifestLoader final : public ManifestLoader
{
public:
    explicit RecipeManifestLoader(CacheRoot cacheToUse);

    std::optional<Project> load(const eacp::FilePath& directory,
                                const Env& env,
                                std::string& error) override;

    // As load, for a recipe file of any name; a missing one is an error.
    std::optional<Project>
        loadFile(const eacp::FilePath& recipe, const Env& env, std::string& error);

private:
    CacheRoot cache;
};
} // namespace epm
