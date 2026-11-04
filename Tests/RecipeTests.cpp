#include <epm/Emit/RecipeSdkCMake.h>
#include <epm/Graph/ManifestLoader.h>
#include <epm/Recipe/RecipeBuild.h>
#include <epm/Recipe/RecipeLoader.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

#include <filesystem>

using namespace nano;
using namespace epm;
using eacp::FilePath;

namespace
{
FilePath recipeFixture()
{
    return FilePath {EPM_TEST_RECIPE_DIR} / "epm.cpp";
}

// The recipe tests compile with the toolchain that built epm; a binary moved
// to another machine may not find it there. Windows has no recipes yet.
bool hasRecipeCompiler()
{
    if (eacp::Platform::isWindows())
    {
        eacp::Console::print("Recipe: skipped, no recipes on Windows");
        return false;
    }

    if (eacp::Files::isFile(recipeSdk().compiler))
        return true;

    eacp::Console::print("Recipe: skipped, no compiler at " + recipeSdk().compiler);
    return false;
}

Env envForSystem(std::string systemName)
{
    auto env = Env {};
    env.systemName = std::move(systemName);
    return env;
}

std::filesystem::file_time_type modified(const FilePath& path)
{
    return std::filesystem::last_write_time(eacp::toStdPath(path));
}
} // namespace

auto tRecipeDescribes = test("Recipe/buildLoadDescribe") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto build = buildRecipe(recipeFixture(), CacheRoot {root.path()});

    check(build.ok);
    check(!build.cached);
    check(eacp::Files::isFile(build.module));
    check(eacp::Files::isFile(build.log));

    // The recipe header compiles clean under epm's own warning flags.
    check(eacp::Files::readFile(build.log).find("warning:") == std::string::npos);

    auto load = loadRecipe(build.module);
    check(load.error.empty());

    if (!load.recipe)
        return;

    const auto mac = load.recipe->describe(envForSystem("Darwin"));
    check(mac.packages.size() == 1);
    check(mac.packages[0].name == "fmt");
    check(mac.packages[0].git == "fmtlib/fmt");
    check(mac.packages[0].tag == "11.0.2");
    check(mac.packages[0].options.at("FMT_DOC") == "OFF");

    const auto linux = load.recipe->describe(envForSystem("Linux"));
    check(linux.packages.size() == 2);

    if (linux.packages.size() == 2)
    {
        check(linux.packages[1].name == "uuid");
        check(linux.packages[1].git == "/opt/epm/libuuid");
        check(linux.packages[1].mode == "source");
        check(linux.packages[1].configs.size() == 1);
    }
};

auto tRecipeCacheHit = test("Recipe/cacheHit") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto cache = CacheRoot {root.path()};
    const auto first = buildRecipe(recipeFixture(), cache);
    check(first.ok);

    if (!first.ok)
        return;

    const auto logWritten = modified(first.log);
    const auto second = buildRecipe(recipeFixture(), cache);

    check(second.ok);
    check(second.cached);
    check(second.module == first.module);
    check(modified(first.log) == logWritten);
};

auto tRecipeKey = test("Recipe/key") = []
{
    const auto& sdk = recipeSdk();
    const auto key = recipeKey(sdk, "epm-a", "source");

    check(key.size() == 64);
    check(key == recipeKey(sdk, "epm-a", "source"));
    check(key != recipeKey(sdk, "epm-b", "source"));
    check(key != recipeKey(sdk, "epm-a", "source "));
};

auto tRecipeCompileArguments = test("Recipe/compileArguments") = []
{
    auto sdk = RecipeSdk {};
    sdk.cxxStandard = "20";
    sdk.cxxExtensions = false;
    sdk.flags = {"-g"};
    sdk.includeDirectories = {"/inc"};
    sdk.compileDefinitions = {"NAME=\"value\""};

    const auto arguments = recipeCompileArguments(sdk, "/src/epm.cpp", "/out/m.so");
    const auto line = eacp::Strings::join(arguments, " ");

    check(arguments[0] == "-std=c++20");
    check(line.find("-shared -fPIC -fvisibility=hidden -g -O1")
          != std::string::npos);
    check(line.find("-I/inc -DNAME=\"value\"") != std::string::npos);
    check(line.ends_with("-o /out/m.so /src/epm.cpp"));
};

auto tRecipeSdkCMake = test("Recipe/sdkCMake") = []
{
    auto sdk = RecipeSdk {};
    sdk.compiler = "/opt/my $compilers/c++";
    sdk.compilerId = "Clang";
    sdk.cxxStandard = "20";
    sdk.cxxExtensions = false;
    sdk.includeDirectories = {"/inc one", "/inc;two"};
    sdk.compileDefinitions = {"NAME=\"value\""};
    sdk.compileOptions = {"-Wall"};

    const auto text = recipeSdkCMakeText(sdk);

    check(text.find("set(EPM_RECIPE_CXX_COMPILER \"/opt/my \\$compilers/c++\")\n")
          != std::string::npos);
    check(text.find("set(EPM_RECIPE_COMPILER_ID \"Clang\")\n") != std::string::npos);
    check(text.find("set(EPM_RECIPE_CXX_STANDARD \"20\")\n") != std::string::npos);
    check(text.find("set(EPM_RECIPE_CXX_EXTENSIONS OFF)\n") != std::string::npos);
    check(text.find("set(EPM_RECIPE_INCLUDE_DIRS \"/inc one;/inc\\\\;two\")\n")
          != std::string::npos);
    check(text.find("set(EPM_RECIPE_COMPILE_DEFINITIONS \"NAME=\\\"value\\\"\")\n")
          != std::string::npos);
    check(text.find("set(EPM_RECIPE_COMPILE_OPTIONS \"-Wall\")\n")
          != std::string::npos);
};

auto tRecipeSyntaxError = test("Recipe/syntaxError") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto source = root.path() / "src" / "epm.cpp";
    eacp::Files::writeFile(source,
                           "#include <epm/Recipe/Recipe.h>\nthis is not C++\n");

    const auto build = buildRecipe(source, CacheRoot {root.path() / "cache"});

    check(!build.ok);
    check(eacp::Files::isFile(build.log));
    check(!eacp::Files::isFile(build.module));
    check(build.error.find(build.log.str()) != std::string::npos);
    check(!build.details.empty());
    check(build.firstDiagnostic.find("error:") != std::string::npos);
    check(build.firstDiagnostic.find("epm.cpp:2:") != std::string::npos);
};

auto tRecipeManifestLoader = test("Recipe/manifestLoader/describesForTheEnv") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    auto loader = RecipeManifestLoader {CacheRoot {root.path()}};
    auto error = std::string {};

    const auto linux =
        loader.load(recipeFixture().parentDirectory(), envForSystem("Linux"), error);
    check(error.empty());
    check(linux.has_value() && linux->packages.size() == 2);

    const auto none =
        loader.load(root.path() / "no-recipe-here", envForSystem("Linux"), error);
    check(none.has_value() && none->packages.empty());
};

auto tRecipeManifestLoaderFails =
    test("Recipe/manifestLoader/reportsTheCompiler") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto directory = root.path() / "package";
    eacp::Files::writeFile(directory / "epm.cpp",
                           "#include <epm/Recipe/Recipe.h>\nthis is not C++\n");

    auto loader = RecipeManifestLoader {CacheRoot {root.path() / "cache"}};
    auto error = std::string {};
    const auto project = loader.load(directory, Env {}, error);

    check(!project.has_value());
    check(error.find("did not compile") != std::string::npos);
    check(error.find("compile.log") != std::string::npos);
    check(error.find("epm.cpp:2:") != std::string::npos);
    check(error.find("error:") != std::string::npos);
};

auto tRecipeNoEntry = test("Recipe/noEntryPoint") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto source = root.path() / "src" / "epm.cpp";
    eacp::Files::writeFile(source, "int notARecipe() { return 1; }\n");

    const auto build = buildRecipe(source, CacheRoot {root.path() / "cache"});
    check(build.ok);

    if (!build.ok)
        return;

    const auto load = loadRecipe(build.module);
    check(!load.recipe);
    check(load.error.find("epmRecipeEntry") != std::string::npos);
};

auto tRecipeMissingSource = test("Recipe/missingSource") = []
{
    if (eacp::Platform::isWindows())
        return;

    auto root = eacp::TemporaryFile {};
    const auto build = buildRecipe(root.path() / "epm.cpp", CacheRoot {root.path()});

    check(!build.ok);
    check(build.error.find("no recipe at") != std::string::npos);
};
