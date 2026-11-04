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

// The recipe tests compile with the compiler that built them, as a configure
// passes its own; a binary moved to another machine may not find it there.
// Windows has no recipes yet.
bool hasRecipeCompiler()
{
    if (eacp::Platform::isWindows())
    {
        eacp::Console::print("Recipe: skipped, no recipes on Windows");
        return false;
    }

    if (eacp::Files::isFile(FilePath {EPM_TEST_CXX_COMPILER}))
        return true;

    eacp::Console::print(std::string {"Recipe: skipped, no compiler at "}
                         + EPM_TEST_CXX_COMPILER);
    return false;
}

Env envForSystem(std::string systemName)
{
    auto env = Env {};
    env.systemName = std::move(systemName);
    env.recipeCompiler = EPM_TEST_CXX_COMPILER;
    env.cxxCompilerId = EPM_TEST_CXX_COMPILER_ID;
    return env;
}

Env compilingEnv()
{
    return envForSystem({});
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
    const auto build =
        buildRecipe(recipeFixture(), CacheRoot {root.path()}, compilingEnv());

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

    const auto linuxProject = load.recipe->describe(envForSystem("Linux"));
    check(linuxProject.packages.size() == 2);

    if (linuxProject.packages.size() == 2)
    {
        check(linuxProject.packages[1].name == "uuid");
        check(linuxProject.packages[1].git == "/opt/epm/libuuid");
        check(linuxProject.packages[1].mode == "source");
        check(linuxProject.packages[1].configs.size() == 1);
    }
};

auto tRecipeCacheHit = test("Recipe/cacheHit") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto cache = CacheRoot {root.path()};
    const auto first = buildRecipe(recipeFixture(), cache, compilingEnv());
    check(first.ok);

    if (!first.ok)
        return;

    const auto logWritten = modified(first.log);
    const auto second = buildRecipe(recipeFixture(), cache, compilingEnv());

    check(second.ok);
    check(second.cached);
    check(second.module == first.module);
    check(modified(first.log) == logWritten);
};

auto tRecipeKey = test("Recipe/key") = []
{
    const auto& sdk = recipeSdk();
    auto toolchain = RecipeToolchain {};
    toolchain.compiler = "/usr/bin/c++";
    const auto key = recipeKey(sdk, toolchain, "epm-a", "source");

    check(key.size() == 64);
    check(key == recipeKey(sdk, toolchain, "epm-a", "source"));
    check(key != recipeKey(sdk, toolchain, "epm-b", "source"));
    check(key != recipeKey(sdk, toolchain, "epm-a", "source "));

    auto otherCompiler = toolchain;
    otherCompiler.compiler = "/opt/gcc/bin/g++";
    check(key != recipeKey(sdk, otherCompiler, "epm-a", "source"));
};

auto tRecipeCompileArguments = test("Recipe/compileArguments") = []
{
    auto sdk = RecipeSdk {};
    sdk.cxxStandard = "20";
    sdk.cxxExtensions = false;
    sdk.flags = {"-g"};
    sdk.compileDefinitions = {"NAME=\"value\""};
    auto toolchain = RecipeToolchain {};
    toolchain.includeDirectory = FilePath {"/inc"};

    const auto arguments =
        recipeCompileArguments(sdk, toolchain, "/src/epm.cpp", "/out/m.so");
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
    sdk.compilerId = "Clang";
    sdk.cxxStandard = "20";
    sdk.cxxExtensions = false;
    sdk.compileDefinitions = {"NAME=\"value\""};
    sdk.compileOptions = {"-Wall"};

    const auto text = recipeSdkCMakeText(sdk, FilePath {"/cache/my $sdk/include"});

    check(text.find("EPM_RECIPE_CXX_COMPILER") == std::string::npos);
    check(text.find("set(EPM_RECIPE_COMPILER_ID \"Clang\")\n") != std::string::npos);
    check(text.find("set(EPM_RECIPE_CXX_STANDARD \"20\")\n") != std::string::npos);
    check(text.find("set(EPM_RECIPE_CXX_EXTENSIONS OFF)\n") != std::string::npos);
    check(text.find("set(EPM_RECIPE_INCLUDE_DIRS \"/cache/my \\$sdk/include\")\n")
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

    const auto build =
        buildRecipe(source, CacheRoot {root.path() / "cache"}, compilingEnv());

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

    const auto linuxProject =
        loader.load(recipeFixture().parentDirectory(), envForSystem("Linux"), error);
    check(error.empty());
    check(linuxProject.has_value() && linuxProject->packages.size() == 2);

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
    const auto project = loader.load(directory, compilingEnv(), error);

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

    const auto build =
        buildRecipe(source, CacheRoot {root.path() / "cache"}, compilingEnv());
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
    const auto build = buildRecipe(
        root.path() / "epm.cpp", CacheRoot {root.path()}, compilingEnv());

    check(!build.ok);
    check(build.error.find("no recipe at") != std::string::npos);
};
