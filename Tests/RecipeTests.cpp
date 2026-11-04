#include <epm/Emit/GraphResponse.h>
#include <epm/Emit/RecipeSdkCMake.h>
#include <epm/Graph/ManifestLoader.h>
#include <epm/Recipe/Recipe.h>
#include <epm/Recipe/RecipeBuild.h>
#include <epm/Recipe/RecipeRun.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Spec/Overrides.h>
#include <epm/Spec/Platform.h>
#include <epm/Util/Command.h>
#include <epm/Util/Paths.h>
#include <epm/Util/Text.h>

#include <eacp/Core/App/Args.h>
#include <eacp/Core/Platform/Platform.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

#include <csignal>
#include <filesystem>
#include <optional>
#include <string>

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
// to another machine may not find it there. Windows has no recipes until
// RecipeBuild has an MSVC argument shape.
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

Env envForProfile(std::string profile)
{
    auto env = Env {};
    env.profile = std::move(profile);
    return env;
}

std::filesystem::file_time_type modified(const FilePath& path)
{
    return std::filesystem::last_write_time(eacp::toStdPath(path));
}

// What recipeMain runs in-process, as recipeMain<TwiceFmt> would make it.
struct TwiceFmt final : Recipe
{
    void describe() override
    {
        add("fmt", "fmtlib/fmt", env().profile);

        if (env().profile == "twice")
            add("fmt");
    }
};

std::unique_ptr<Recipe> createTwiceFmt()
{
    return std::make_unique<TwiceFmt>();
}

// Which steps `build --package` picked, told apart by what they throw.
struct CustomSteps final : Package
{
    void build() override { throw BuildError {"the custom steps ran"}; }
};

struct PicksSteps final : Recipe
{
    void describe() override
    {
        add<CustomSteps>("Custom");
        add("Plain");
    }

    void build() override { throw BuildError {"the recipe's own steps ran"}; }
};

std::unique_ptr<Recipe> createPicksSteps()
{
    return std::make_unique<PicksSteps>();
}

// recipeMain in-process, started in `workingDirectory` (this one when empty),
// which is restored after: configure enters the manifest's directory.
int runRecipeMain(eacp::Vector<std::string> arguments,
                  RecipeFactory create = createTwiceFmt,
                  const FilePath& workingDirectory = {})
{
    arguments.insert(0, "epm-recipe");
    auto argv = eacp::Vector<char*> {};

    for (auto& argument: arguments)
        argv.add(argument.data());

    const auto previous = std::filesystem::current_path();

    if (!workingDirectory.empty())
        std::filesystem::current_path(eacp::toStdPath(workingDirectory));

    const auto code = recipeMain(int(argv.size()), argv.data(), create);
    std::filesystem::current_path(previous);
    return code;
}

// A build input for `name` in source mode, which needs neither git nor cmake.
BuildInput sourceInput(const FilePath& root, std::string name)
{
    auto input = BuildInput {};
    input.recipeName = "Self";
    input.cacheRoot = (root / "cache").str();
    input.request.name = std::move(name);
    input.request.mode = "source";
    input.request.configs = {"Release"};
    input.source = BuildSource {{}, {}, (root / "src").str()};
    return input;
}

// What `build --package <package>` wrote, run in-process over `input`.
std::optional<BuildOutput> buildPackageMain(const FilePath& root,
                                            const BuildInput& input,
                                            const std::string& package,
                                            int expectedCode)
{
    const auto in = root / "in.json";
    const auto out = root / "out.json";
    eacp::Files::removeFile(out);
    check(saveJsonFile(in, input));
    check(runRecipeMain(
              {"build", "--package", package, "--in", in.str(), "--out", out.str()},
              createPicksSteps)
          == expectedCode);

    return loadJsonFile<BuildOutput>(out);
}

// A static library with one source that fails to configure without
// TINY_FLAG, and whose INTERFACE_COMPILE_DEFINITIONS carry TINY_VALUE.
FilePath writeTinyProject(const FilePath& directory)
{
    check(writeTextFile(directory / "CMakeLists.txt",
                        "cmake_minimum_required(VERSION 3.25)\n"
                        "project(Tiny CXX)\n"
                        "if (NOT TINY_FLAG)\n"
                        "    message(FATAL_ERROR \"TINY_FLAG is not set\")\n"
                        "endif ()\n"
                        "if (NOT DEFINED TINY_VALUE)\n"
                        "    set(TINY_VALUE 1)\n"
                        "endif ()\n"
                        "add_library(Tiny STATIC Tiny.cpp)\n"
                        "target_compile_definitions(Tiny PUBLIC "
                        "TINY_VALUE=${TINY_VALUE})\n"));
    check(writeTextFile(directory / "Tiny.cpp",
                        "int tiny() { return TINY_VALUE; }\n"));
    return directory;
}

bool hasTestCMake()
{
    if (eacp::Files::isFile(FilePath {EPM_TEST_CMAKE}))
        return true;

    eacp::Console::print("Recipe: skipped, no cmake at " EPM_TEST_CMAKE);
    return false;
}

// Tiny prebuilt, from writeTinyProject under <work>/src, for
// Tests/Recipe/epm.cpp's profile "build", which declares it with add<Tiny>.
BuildInput tinyInput(const FilePath& work, const CacheRoot& cache)
{
    auto input = BuildInput {};
    input.env.cmake = EPM_TEST_CMAKE;
    input.env.cxxCompiler = recipeSdk().compiler;
    input.env.buildType = "Release";
    input.env.systemName = eacp::Platform::isMac() ? "Darwin" : "Linux";
    input.env.systemProcessor = "test";
    input.env.profile = "build";
    input.cacheRoot = cache.path().str();
    input.request.name = "Tiny";
    input.request.gitUrl = "https://example.invalid/tiny.git";
    input.request.gitRef = "v1";
    input.request.mode = "prebuilt";
    input.request.configs = {"Release"};
    input.request.options = {{"TINY_FLAG", "ON"}};
    input.source =
        BuildSource {{}, "tiny-source", writeTinyProject(work / "src").str()};
    input.epmExecutable = eacp::Files::executablePath().str();
    return input;
}
} // namespace

auto tRecipeBuildsAndRuns = test("Recipe/buildAndRun") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto build = buildRecipe(recipeFixture(), CacheRoot {root.path()});

    check(build.ok);
    check(!build.cached);
    check(eacp::Files::isFile(build.executable));
    check(eacp::Files::isFile(build.log));

    // The recipe header compiles clean under epm's own warning flags.
    check(eacp::Files::readFile(build.log).find("warning:") == std::string::npos);

    if (!build.ok)
        return;

    const auto mac = runRecipe(build.executable, envForSystem("Darwin"));
    check(mac.error.empty());
    check(mac.project.has_value());

    if (mac.project)
    {
        check(mac.project->packages.size() == 1);
        check(mac.project->packages[0].name == "fmt");
        check(mac.project->packages[0].git == "fmtlib/fmt");
        check(mac.project->packages[0].tag == "11.0.2");
        check(mac.project->packages[0].options.at("FMT_DOC") == "OFF");
    }

    const auto linux = runRecipe(build.executable, envForSystem("Linux"));
    check(linux.project.has_value() && linux.project->packages.size() == 2);

    if (linux.project && linux.project->packages.size() == 2)
    {
        const auto& uuid = linux.project->packages[1];
        check(uuid.name == "uuid");
        check(uuid.git == "/opt/epm/libuuid");
        check(uuid.mode == "source");
        check(uuid.configs.size() == 1);
    }

    // eacp::Processes, from inside the recipe.
    const auto shell = runRecipe(build.executable, envForProfile("shell"));
    check(shell.project.has_value() && shell.project->packages.size() == 2);

    if (shell.project && shell.project->packages.size() == 2)
        check(shell.project->packages[1].name == "shell");

    const auto thrown = runRecipe(build.executable, envForProfile("throw"));
    check(!thrown.project);
    check(thrown.error.find("exited with code 1") != std::string::npos);
    check(thrown.error.find("asked to throw") != std::string::npos);

    const auto aborted = runRecipe(build.executable, envForProfile("abort"));
    check(!aborted.project);
    check(aborted.error.find("terminated by signal " + std::to_string(SIGABRT))
          != std::string::npos);

    const auto exited = runRecipe(build.executable, envForProfile("exit0"));
    check(!exited.project);
    check(exited.error.find("exited without writing its project")
          != std::string::npos);
    check(exited.error.find("EPM_RECIPE") == std::string::npos);

    // In the directory it is given, else in this process's own.
    const auto inFixture = runRecipe(
        build.executable, envForProfile("cwd"), recipeFixture().parentDirectory());
    check(inFixture.project.has_value() && inFixture.project->packages.size() == 2);

    if (inFixture.project && inFixture.project->packages.size() == 2)
        check(inFixture.project->packages[1].name == "Recipe");

    // A relative path is not looked up on PATH.
    const auto relative = std::filesystem::relative(
        eacp::toStdPath(build.executable), std::filesystem::current_path());
    const auto fromRelative = runRecipe(FilePath {relative}, envForSystem("Darwin"));
    check(fromRelative.error.empty());
    check(fromRelative.project.has_value());
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
    check(second.executable == first.executable);
    check(modified(first.log) == logWritten);
};

auto tRecipeKey = test("Recipe/key") = []
{
    const auto& sdk = recipeSdk();
    const auto key = recipeKey(sdk, "epm-a", "inputs-a", "source");

    check(key.size() == 64);
    check(key == recipeKey(sdk, "epm-a", "inputs-a", "source"));
    check(key != recipeKey(sdk, "epm-b", "inputs-a", "source"));
    check(key != recipeKey(sdk, "epm-a", "inputs-b", "source"));
    check(key != recipeKey(sdk, "epm-a", "inputs-a", "source "));
};

auto tRecipeInputsHash = test("Recipe/inputsHash") = []
{
    auto root = eacp::TemporaryFile {};
    const auto include = root.path() / "include";
    const auto archive = root.path() / "lib" / "libepm-recipe.a";

    auto sdk = RecipeSdk {};
    sdk.config = "Release";
    sdk.includeDirectories = {include.str()};
    sdk.libraries["Release"] = {archive.str()};

    check(!recipeInputsHash(sdk).has_value());

    check(writeTextFile(archive, "one"));
    check(writeTextFile(include / "epm" / "Recipe" / "Recipe.h", "#define A"));
    const auto first = recipeInputsHash(sdk);
    check(first.has_value());
    check(first == recipeInputsHash(sdk));

    check(writeTextFile(archive, "two"));
    const auto rebuilt = recipeInputsHash(sdk);
    check(rebuilt.has_value() && rebuilt != first);

    check(writeTextFile(include / "epm" / "Recipe" / "RecipeMain.h", "int f();"));
    const auto edited = recipeInputsHash(sdk);
    check(edited.has_value() && edited != rebuilt);

    if (first && rebuilt)
        check(recipeKey(sdk, "epm", *first, "source")
              != recipeKey(sdk, "epm", *rebuilt, "source"));

    check(recipeInputsHash(recipeSdk()).has_value());
};

// A target name (Threads::Threads) out of the walk's scope would reach the
// linker as -lThreads::Threads.
auto tRecipeLinkOptionsAreNoTargets = test("Recipe/linkOptionsAreNoTargets") = []
{
    for (const auto& option: bakedRecipeSdk().sdk.linkOptions)
        check(option.find("::") == std::string::npos);

    for (const auto& option: recipeSdk().linkOptions)
        check(option.find("::") == std::string::npos);
};

auto tRecipeSdkHasLibraries = test("Recipe/sdkHasLibraries") = []
{
    const auto& sdk = recipeSdk();
    const auto libraries = recipeLibraries(sdk);

    check(!sdk.config.empty());
    check(recipeConfigs(sdk).contains(sdk.config));
    check(libraries.size() >= 2);

    for (const auto& library: libraries)
        check(eacp::Files::isFile(library));

    if (!libraries.empty())
        check(FilePath {libraries[0]}.stem().find("epm-recipe")
              != std::string::npos);
};

auto tRecipeSdkFor = test("Recipe/sdkFor") = []
{
    const auto& baked = bakedRecipeSdk();

    check(recipeSdkFor(eacp::Files::executablePath()).identity
          == recipeSdk().identity);

    auto root = eacp::TemporaryFile {};
    const auto prefix = resolvedPath(root.path());
    const auto epm = prefix / "bin" / "epm";
    check(writeTextFile(epm, "epm"));

    const auto bare = recipeSdkFor(epm);
    check(bare.includeDirectories == baked.sdk.includeDirectories);
    check(recipeLibraries(bare) == recipeLibraries(baked.sdk));

    const auto include = prefix / baked.includeDestination;
    const auto libraries = prefix / baked.libraryDestination / baked.sdk.config;
    check(writeTextFile(include / "epm" / "Recipe" / "Recipe.h", "#pragma once"));

    for (const auto& name: baked.installedLibraries)
        check(writeTextFile(libraries / name, "archive"));

    const auto installed = recipeSdkFor(epm);
    check(installed.includeDirectories == eacp::Vector<std::string> {include.str()});
    check(recipeConfigs(installed) == eacp::Vector<std::string> {baked.sdk.config});

    for (const auto& library: recipeLibraries(installed))
        check(FilePath {library}.parentDirectory() == libraries);

    check(installed.identity != bare.identity);
};

auto tRecipeCompileArguments = test("Recipe/compileArguments") = []
{
    auto sdk = RecipeSdk {};
    sdk.cxxStandard = "20";
    sdk.cxxExtensions = false;
    sdk.config = "Debug";
    sdk.flags = {"-g"};
    sdk.includeDirectories = {"/inc"};
    sdk.compileDefinitions = {"NAME=\"value\""};
    sdk.libraries["Debug"] = {"/lib/Debug/libepm-recipe.a", "/lib/Debug/libeacp.a"};
    sdk.libraries["Release"] = {"/lib/Release/libepm-recipe.a"};
    sdk.linkOptions = {"-framework Foundation", "pthread", "-lobjc"};

    const auto arguments =
        recipeCompileArguments(sdk, "/src/epm.cpp", "/out/epm-recipe.tmp");
    const auto line = joinList(arguments, " ");

    check(arguments[0] == "-std=c++20");
    check(line.find("-shared") == std::string::npos);
    check(line.find("-fPIC") == std::string::npos);
    check(line.find("-undefined") == std::string::npos);
    check(line.find("/lib/Release/") == std::string::npos);
    check(line.find("-std=c++20 -g -O1 -I/inc -DNAME=\"value\"")
          != std::string::npos);
    check(line.ends_with("-o /out/epm-recipe.tmp /src/epm.cpp "
                         "/lib/Debug/libepm-recipe.a /lib/Debug/libeacp.a "
                         "-framework Foundation -lpthread -lobjc"));
    check(arguments.contains("Foundation"));
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
    sdk.libraries["RelWithDebInfo"] = {"/p/lib/RelWithDebInfo/libepm-recipe.a"};
    sdk.libraries["Debug"] = {"/p/lib/Debug/libepm-recipe.a",
                              "/p/lib/Debug/libMiro.a"};
    sdk.linkOptions = {"-framework Foundation"};

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
    check(text.find("set(EPM_RECIPE_CONFIGS \"Debug;RelWithDebInfo\")\n")
          != std::string::npos);
    check(text.find("set(EPM_RECIPE_LINK_LIBRARIES_DEBUG "
                    "\"/p/lib/Debug/libepm-recipe.a;/p/lib/Debug/libMiro.a\")\n")
          != std::string::npos);
    check(text.find("set(EPM_RECIPE_LINK_LIBRARIES_RELWITHDEBINFO "
                    "\"/p/lib/RelWithDebInfo/libepm-recipe.a\")\n")
          != std::string::npos);
    check(text.find("set(EPM_RECIPE_LINK_OPTIONS \"-framework Foundation\")\n")
          != std::string::npos);
};

auto tRecipeInstalledSdk = test("Recipe/installedSdk") = []
{
    auto root = eacp::TemporaryFile {};
    const auto prefix = root.path();

    auto baked = BakedRecipeSdk {};
    baked.sdk.config = "Release";
    baked.sdk.includeDirectories = {"/build/Lib", "/build/_deps/miro/Lib"};
    baked.sdk.libraries["Release"] = {"/build/libepm-recipe.a"};
    baked.includeDestination = "include";
    baked.libraryDestination = "lib";
    baked.installedLibraries = {"libepm-recipe.a", "libMiro.a"};

    check(!installedRecipeSdk(baked, prefix).has_value());

    check(writeTextFile(prefix / "include" / "epm" / "Recipe" / "Recipe.h", ""));

    for (const auto* config: {"Debug", "Release"})
        for (const auto& name: baked.installedLibraries)
            check(writeTextFile(prefix / "lib" / config / name, ""));

    // Missing one archive: not a usable configuration.
    check(writeTextFile(prefix / "lib" / "MinSizeRel" / "libepm-recipe.a", ""));

    const auto sdk = installedRecipeSdk(baked, prefix);
    check(sdk.has_value());

    if (!sdk)
        return;

    check(sdk->includeDirectories.size() == 1);
    check(sdk->includeDirectories[0] == (prefix / "include").str());
    check(recipeConfigs(*sdk) == eacp::Vector<std::string> {"Debug", "Release"});
    check(
        recipeLibraries(*sdk)
        == eacp::Vector<std::string> {(prefix / "lib/Release/libepm-recipe.a").str(),
                                      (prefix / "lib/Release/libMiro.a").str()});

    baked.sdk.config = "MinSizeRel";
    check(!installedRecipeSdk(baked, prefix).has_value());
};

auto tRecipeProjectJson = test("Recipe/projectJsonRoundTrip") = []
{
    auto project = Project {};
    setAbiMatch(setMode(project.add("fmt", "fmtlib/fmt", "11.0.2"), "source"),
                "any");
    addConfig(setOption(project.add("zlib"), "ZLIB_BUILD_EXAMPLES", "OFF"), "Debug");

    auto root = eacp::TemporaryFile {};
    const auto file = root.path() / "project.json";
    check(saveJsonFile(file, project));

    const auto loaded = loadJsonFile<Project>(file);
    check(loaded.has_value());

    if (!loaded)
        return;

    check(Miro::toJSONString(*loaded, 2) == Miro::toJSONString(project, 2));
    check(loaded->packages.size() == 2);

    if (loaded->packages.size() == 2)
    {
        check(loaded->packages[0].mode == "source");
        check(loaded->packages[0].abiMatch == "any");
        check(loaded->packages[1].options.at("ZLIB_BUILD_EXAMPLES") == "OFF");
        check(loaded->packages[1].configs[0] == "Debug");
    }
};

auto tRecipeMainWrites = test("Recipe/main/writesTheProject") = []
{
    auto root = eacp::TemporaryFile {};
    auto env = Env {};
    env.profile = "1.2.3";
    check(saveJsonFile(root.path() / "env.json", env));

    const auto out = root.path() / "project.json";
    check(runRecipeMain({"describe",
                         "--env",
                         (root.path() / "env.json").str(),
                         "--out",
                         out.str()})
          == 0);

    const auto project = loadJsonFile<Project>(out);
    check(project.has_value() && project->packages.size() == 1);

    if (project && project->packages.size() == 1)
        check(project->packages[0].tag == "1.2.3");
};

auto tRecipeMainFails = test("Recipe/main/fails") = []
{
    auto root = eacp::TemporaryFile {};
    const auto out = root.path() / "project.json";

    check(runRecipeMain({"describe",
                         "--env",
                         (root.path() / "none.json").str(),
                         "--out",
                         out.str()})
          == 1);
    check(!eacp::Files::isFile(out));

    auto env = Env {};
    env.profile = "twice";
    check(saveJsonFile(root.path() / "env.json", env));
    check(runRecipeMain({"describe",
                         "--env",
                         (root.path() / "env.json").str(),
                         "--out",
                         out.str()})
          == 1);
    check(!eacp::Files::isFile(out));
};

// Without a command it configures, which needs --overrides and --out: bare
// describe arguments are not enough, and write nothing.
auto tRecipeMainDefault = test("Recipe/main/configureIsTheDefault") = []
{
    auto root = eacp::TemporaryFile {};
    check(saveJsonFile(root.path() / "env.json", Env {}));
    const auto out = root.path() / "project.json";

    check(runRecipeMain(
              {"--env", (root.path() / "env.json").str(), "--out", out.str()})
          != 0);
    check(!eacp::Files::exists(out));

    // Nothing, or a command it does not know, is a usage error.
    check(runRecipeMain({}) == eacp::Args::badArguments);
    check(runRecipeMain({"descibe", "--out", out.str()})
          == eacp::Args::badArguments);
    check(!eacp::Files::exists(out));
};

// A configure in this process over an empty project: the manifest named, its
// env and overrides read, the graph files written.
auto tRecipeMainConfigureEmpty = test("Recipe/main/configureWritesTheGraph") = []
{
    auto root = eacp::TemporaryFile {};
    const auto work = resolvedPath(root.path());
    const auto manifest = work / "project" / "epm.cpp";
    check(writeTextFile(manifest, "// described in-process\n"));
    check(saveJsonFile(work / "env.json", Env {}));
    check(saveJsonFile(work / "overrides.json", Overrides {}));

    const auto arguments = [&](const FilePath& env)
    {
        return eacp::Vector<std::string> {"configure",
                                          "--manifest",
                                          manifest.str(),
                                          "--env",
                                          env.str(),
                                          "--overrides",
                                          (work / "overrides.json").str(),
                                          "--out",
                                          (work / "out").str(),
                                          "--cache",
                                          (work / "cache").str()};
    };

    // PicksSteps declares Custom and Plain without git: the graph says so.
    check(runRecipeMain(arguments(work / "env.json"), createPicksSteps) == 1);
    check(!eacp::Files::exists(work / "out" / "graph.json"));

    check(runRecipeMain(arguments(work / "none.json"), createTwiceFmt) == 1);

    // TwiceFmt with an empty profile: fmt has no tag, a failure too, so an
    // empty project stands in for one that resolves.
    const auto createEmpty = []() -> std::unique_ptr<Recipe>
    {
        struct Empty final : Recipe
        {
            void describe() override {}
        };

        return std::make_unique<Empty>();
    };

    check(runRecipeMain(arguments(work / "env.json"), createEmpty) == 0);
    check(eacp::Files::isFile(work / "out" / "graph.json"));
    check(eacp::Files::isFile(work / "out" / "graph.cmake"));
};

// Started elsewhere, as an IDE starts it in the build directory, with the
// other paths relative to there: the same graph as from the manifest's
// directory, which describe() and the steps ran in.
auto tRecipeMainConfigureCwd =
    test("Recipe/main/configureEntersTheManifestDirectory") = []
{
    auto root = eacp::TemporaryFile {};
    const auto work = resolvedPath(root.path());
    const auto project = work / "project";
    const auto manifest = project / "epm.cpp";
    check(writeTextFile(manifest, "// described in-process\n"));
    check(eacp::Files::createDirectories(work / "src"));
    check(saveJsonFile(work / "env.json", Env {}));

    // The package is named after the working directory; only "project" has
    // a checkout, which makes it source mode: no fetch, no CMake.
    auto overrides = Overrides {};
    overrides.packages["project"].source = (work / "src").str();
    check(saveJsonFile(work / "overrides.json", overrides));

    const auto createNamedAfterCwd = []() -> std::unique_ptr<Recipe>
    {
        struct NamedAfterCwd final : Recipe
        {
            void describe() override
            {
                add(std::filesystem::current_path().filename().string());
            }
        };

        return std::make_unique<NamedAfterCwd>();
    };

    const auto fromProject =
        eacp::Vector<std::string> {"configure",
                                   "--manifest",
                                   manifest.str(),
                                   "--env",
                                   (work / "env.json").str(),
                                   "--overrides",
                                   (work / "overrides.json").str(),
                                   "--out",
                                   (work / "out").str(),
                                   "--cache",
                                   (work / "cache").str()};
    check(runRecipeMain(fromProject, createNamedAfterCwd, project) == 0);

    const auto graph = eacp::Files::readFile(work / "out" / "graph.json");
    check(graph.find("\"project\"") != std::string::npos);

    const auto fromWork = eacp::Vector<std::string> {"configure",
                                                     "--manifest",
                                                     manifest.str(),
                                                     "--env",
                                                     "env.json",
                                                     "--overrides",
                                                     "overrides.json",
                                                     "--out",
                                                     "out",
                                                     "--cache",
                                                     "cache"};
    check(eacp::Files::removeFile(work / "out" / "graph.json"));
    check(runRecipeMain(fromWork, createNamedAfterCwd, work) == 0);
    check(eacp::Files::readFile(work / "out" / "graph.json") == graph);

    const auto command =
        eacp::Files::readFile(work / "out" / "steps" / "project.command");
    check(command.starts_with("# run from: " + project.str() + "\n"));
};

// `--out p.json`: a file name without a directory, in the working directory.
auto tRecipeMainBareOut = test("Recipe/main/outIsABareFileName") = []
{
    auto root = eacp::TemporaryFile {};
    check(eacp::Files::createDirectories(root.path()));
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(eacp::toStdPath(root.path()));

    const auto code = runRecipeMain({"describe", "--out", "p.json"});
    std::filesystem::current_path(previous);

    check(code == 0);
    check(loadJsonFile<Project>(root.path() / "p.json").has_value());
};

auto tRecipeMainDescribe = test("Recipe/main/describeSubcommand") = []
{
    auto root = eacp::TemporaryFile {};
    const auto out = root.path() / "project.json";

    check(runRecipeMain({"describe", "--out", out.str()}, createPicksSteps) == 0);

    const auto project = loadJsonFile<Project>(out);
    check(project.has_value() && project->packages.size() == 2);

    if (project && project->packages.size() == 2)
    {
        check(project->packages[0].customBuild);
        check(!project->packages[1].customBuild);
    }
};

auto tRecipeMainBuildPicks = test("Recipe/main/buildPicksTheSteps") = []
{
    auto root = eacp::TemporaryFile {};

    const auto custom = buildPackageMain(
        root.path(), sourceInput(root.path(), "Custom"), "Custom", 1);
    check(custom.has_value() && custom->error == "the custom steps ran");

    const auto self =
        buildPackageMain(root.path(), sourceInput(root.path(), "Self"), "Self", 1);
    check(self.has_value() && self->error == "the recipe's own steps ran");

    // Without steps of its own: the default CMakePackage, here in source mode.
    const auto plain =
        buildPackageMain(root.path(), sourceInput(root.path(), "Plain"), "Plain", 0);
    check(plain.has_value() && plain->ok());

    if (plain)
    {
        check(plain->mode == "source");
        check(plain->sourceDirectory == (root.path() / "src").str());
    }

    // So is a package the recipe never declared.
    const auto other =
        buildPackageMain(root.path(), sourceInput(root.path(), "Other"), "Other", 0);
    check(other.has_value() && other->mode == "source");

    // Unless epm expects custom steps for it, which this recipe lacks.
    auto expectsCustom = sourceInput(root.path(), "Other");
    expectsCustom.customBuild = true;
    const auto missing = buildPackageMain(root.path(), expectsCustom, "Other", 1);
    check(missing.has_value() && !missing->ok());

    if (missing)
        check(missing->error
              == "epm expects custom build steps for Other (add<T>) from this "
                 "recipe, but its describe() registers none for it");

    // Custom steps found are run whatever the flag says.
    auto customInput = sourceInput(root.path(), "Custom");
    customInput.customBuild = true;
    const auto found = buildPackageMain(root.path(), customInput, "Custom", 1);
    check(found.has_value() && found->error == "the custom steps ran");

    // An input of another schema is reported as such.
    auto future = sourceInput(root.path(), "Plain");
    future.schema = buildProtocolSchema + 1;
    const auto schema = buildPackageMain(root.path(), future, "Plain", 1);
    check(!schema.has_value());

    // --package must name the input's package.
    const auto mismatch = buildPackageMain(
        root.path(), sourceInput(root.path(), "Plain"), "Custom", 1);
    check(!mismatch.has_value());

    check(runRecipeMain({"build",
                         "--package",
                         "Plain",
                         "--in",
                         (root.path() / "none.json").str(),
                         "--out",
                         (root.path() / "out.json").str()},
                        createPicksSteps)
          == 1);
};

// The recipe executable runs a package's steps against a real CMake project:
// Tests/Recipe/epm.cpp's Tiny, the default flow plus a configure hook, then
// the same project through the default steps in this process.
auto tRecipeBuildPackage = test("Recipe/buildPackage/tinyCMakeProject") = []
{
    if (!hasRecipeCompiler() || !hasTestCMake())
        return;

    auto root = eacp::TemporaryFile {};
    const auto work = resolvedPath(root.path());
    const auto cache = CacheRoot {work / "cache"};
    const auto recipe = buildRecipe(recipeFixture(), cache);
    check(recipe.ok);

    if (!recipe.ok)
        return;

    const auto input = tinyInput(work, cache);
    const auto in = work / "in.json";
    const auto out = work / "out.json";
    check(saveJsonFile(in, input));

    const auto arguments = eacp::Vector<std::string> {
        "build", "--package", "Tiny", "--in", in.str(), "--out", out.str()};
    const auto run = runCommand(recipe.executable.str(), arguments, work);
    check(run.ok);

    if (!run.ok)
        eacp::Console::printError(run.output);

    const auto output = loadJsonFile<BuildOutput>(out);
    check(output.has_value() && output->ok());

    if (!output || !output->ok())
        return;

    const auto prefix = FilePath {output->prefix};
    check(output->mode == "prebuilt");
    check(output->configs == eacp::Vector<std::string> {"Release"});
    check(eacp::Files::isFile(prefix / "Release.json"));
    check(eacp::Files::isFile(prefix / "epm-targets.cmake"));
    check(eacp::Files::isFile(prefix / "lib" / "Release" / "libTiny.a"));

    // The hook's option reached the child build, and so the fingerprint.
    const auto exported = eacp::Files::readFile(prefix / "export-Release.cmake");
    check(exported.find("TINY_VALUE=42") != std::string::npos);

    const auto entry = readEntry(prefix);
    check(entry.has_value() && entry->fingerprint.options.at("TINY_VALUE") == "42");

    // The build trees are gone once harvested.
    check(eacp::Files::isDirectory(cache.build() / "Tiny" / output->platform));
    check(!eacp::Files::exists(cache.build() / "Tiny" / output->platform
                               / prefix.fileName()));

    // Again: the entry is there, nothing is built.
    const auto again = runCommand(recipe.executable.str(), arguments, work);
    check(again.ok);
    check(again.output.find("building Tiny") == std::string::npos);
    check(loadJsonFile<BuildOutput>(out).value_or(BuildOutput {}).prefix
          == output->prefix);

    // The default steps in this process: no hook, so another entry.
    const auto inProcess = buildInProcess(input);
    check(inProcess.ok());
    check(inProcess.mode == "prebuilt");
    check(inProcess.prefix != output->prefix);

    if (inProcess.ok())
        check(eacp::Files::readFile(FilePath {inProcess.prefix}
                                    / "export-Release.cmake")
                  .find("TINY_VALUE=1")
              != std::string::npos);
};

namespace
{
// The default flow, whose afterBuild hook fails once the configs are built
// and harvested.
struct FailsAfterBuild final : CMakePackage
{
    void afterBuild(const Prefix&) override
    {
        throw BuildError {"afterBuild failed"};
    }
};
} // namespace

// A hook that throws after the build leaves no config in the entry, so the
// next run builds it again instead of taking a half-made entry for complete.
auto tRecipeAfterBuildThrows = test("Recipe/buildPackage/afterBuildThrows") = []
{
    if (!hasRecipeCompiler() || !hasTestCMake())
        return;

    auto root = eacp::TemporaryFile {};
    const auto work = resolvedPath(root.path());
    const auto cache = CacheRoot {work / "cache"};
    const auto input = tinyInput(work, cache);

    auto failing = FailsAfterBuild {};
    const auto failed = runPackage(failing, input);
    check(!failed.ok());
    check(failed.error == "afterBuild failed");

    const auto entries = findEntries(cache, "Tiny", platformName(input.env));
    check(entries.size() == 1);

    if (entries.size() != 1)
        return;

    const auto prefix = entries[0].prefix;
    check(entries[0].configs.empty());
    check(!eacp::Files::exists(prefix / "Release.json"));
    check(!eacp::Files::exists(prefix / "epm-targets.cmake"));

    // The default steps, with the same fingerprint, build into that entry.
    const auto rebuilt = buildInProcess(input);
    check(rebuilt.ok());
    check(FilePath {rebuilt.prefix} == prefix);
    check(rebuilt.configs == eacp::Vector<std::string> {"Release"});
    check(eacp::Files::isFile(prefix / "Release.json"));
};

auto tRecipeSyntaxError = test("Recipe/syntaxError") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto source = root.path() / "src" / "epm.cpp";
    check(
        writeTextFile(source, "#include <epm/Recipe/Recipe.h>\nthis is not C++\n"));

    const auto build = buildRecipe(source, CacheRoot {root.path() / "cache"});

    check(!build.ok);
    check(eacp::Files::isFile(build.log));
    check(!eacp::Files::isFile(build.executable));
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

    // Run in its epm.cpp's directory.
    const auto cwd =
        loader.load(recipeFixture().parentDirectory(), envForProfile("cwd"), error);
    check(cwd.has_value() && cwd->packages.size() == 2);

    if (cwd && cwd->packages.size() == 2)
        check(cwd->packages[1].name == "Recipe");

    const auto thrown = loader.load(
        recipeFixture().parentDirectory(), envForProfile("throw"), error);
    check(!thrown.has_value());
    check(error.find(recipeFixture().str()) != std::string::npos);
    check(error.find("asked to throw") != std::string::npos);
};

auto tRecipeManifestLoaderFails =
    test("Recipe/manifestLoader/reportsTheCompiler") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto directory = root.path() / "package";
    check(writeTextFile(directory / "epm.cpp",
                        "#include <epm/Recipe/Recipe.h>\nthis is not C++\n"));

    auto loader = RecipeManifestLoader {CacheRoot {root.path() / "cache"}};
    auto error = std::string {};
    const auto project = loader.load(directory, Env {}, error);

    check(!project.has_value());
    check(error.find("did not compile") != std::string::npos);
    check(error.find("compile.log") != std::string::npos);
    check(error.find("epm.cpp:2:") != std::string::npos);
    check(error.find("error:") != std::string::npos);
};

// The step through the real executable, as epm configure runs it: its input
// and output kept in the steps directory.
auto tRecipeLoaderStep = test("Recipe/manifestLoader/runsABuildStep") = []
{
    if (!hasRecipeCompiler() || !hasTestCMake())
        return;

    auto root = eacp::TemporaryFile {};
    const auto work = resolvedPath(root.path());
    const auto cache = CacheRoot {work / "cache"};
    const auto steps = work / "epm" / "steps";
    auto loader = RecipeManifestLoader {cache, steps};
    auto input = tinyInput(work, cache);
    input.recipeName = "Recipe";
    auto error = std::string {};

    check(loader.runsSteps());
    const auto output = loader.build(recipeFixture(), input, error);

    check(error.empty());
    check(output.has_value() && output->ok());

    if (!output || !output->ok())
    {
        eacp::Console::printError(error);
        return;
    }

    check(output->mode == "prebuilt");
    check(eacp::Files::isDirectory(FilePath {output->prefix}));
    check(eacp::Files::isFile(FilePath {output->prefix} / "epm-targets.cmake"));

    // The add<Tiny> steps ran, not the default ones.
    const auto entry = readEntry(FilePath {output->prefix});
    check(entry.has_value() && entry->fingerprint.options.at("TINY_VALUE") == "42");

    const auto written = loadJsonFile<BuildInput>(steps / "Tiny.in.json");
    check(written.has_value() && written->recipeName == "Recipe");
    check(loadJsonFile<BuildOutput>(steps / "Tiny.out.json")
              .value_or(BuildOutput {})
              .prefix
          == output->prefix);

    // A step that fails reports it, with the end of the child build's output.
    input.request.options.clear();
    const auto failed = loader.build(recipeFixture(), input, error);
    check(failed.has_value() && !failed->ok());

    if (failed)
        check((failed->error + failed->details).find("TINY_FLAG is not set")
              != std::string::npos);

    // One that dies writes nothing: the error says how it ended.
    input.env.profile = "abort";
    const auto died = loader.build(recipeFixture(), input, error);
    check(!died.has_value());
    check(error.find("build --package Tiny") != std::string::npos);
    check(error.find("terminated by signal") != std::string::npos);
    check(!eacp::Files::exists(steps / "Tiny.out.json"));
};

// A failing step run by epm (--quiet) records its error and prints none of
// it, since epm, which echoes the step's output, reports it; the kept
// <name>.command has no --quiet, so run by hand it prints it.
auto tRecipeStepQuiet = test("Recipe/step/failurePrintedOnce") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto work = resolvedPath(root.path());
    const auto steps = work / "steps";
    auto loader = RecipeManifestLoader {CacheRoot {work / "cache"}, steps};
    auto input = sourceInput(work, "Tiny");
    input.env.profile = "throw";
    auto error = std::string {};

    const auto output = loader.build(recipeFixture(), input, error);
    check(output.has_value() && output->error == "the recipe threw: asked to throw");

    // "# run from: <directory>", then the command.
    const auto commandFile = eacp::Files::readFile(steps / "Tiny.command");
    const auto firstLineEnd = commandFile.find('\n');
    check(firstLineEnd != std::string::npos);

    if (firstLineEnd == std::string::npos)
        return;

    const auto prefix = std::string {"# run from: "};
    const auto runFrom =
        FilePath {commandFile.substr(prefix.size(), firstLineEnd - prefix.size())};
    auto command = commandFile.substr(firstLineEnd + 1);

    while (!command.empty() && command.back() == '\n')
        command.pop_back();

    check(command.find("--quiet") == std::string::npos);

    const auto runInShell = [&](const std::string& line)
    { return runCommand("/bin/sh", {"-c", line}, runFrom); };

    const auto byHand = runInShell(command);
    check(!byHand.ok && byHand.exitCode == 1);
    check(byHand.output.find("epm-recipe: the recipe threw: asked to throw")
          != std::string::npos);

    const auto quiet = runInShell(command + " --quiet");
    check(!quiet.ok && quiet.exitCode == 1);
    check(quiet.output.find("asked to throw") == std::string::npos);
    check(loadJsonFile<BuildOutput>(steps / "Tiny.out.json")
              .value_or(BuildOutput {})
              .error
          == "the recipe threw: asked to throw");
};

// A step that exits 0 without writing its output is a failure, named.
auto tRecipeStepNoOutput = test("Recipe/step/exitsWithoutOutput") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto work = resolvedPath(root.path());
    auto loader = RecipeManifestLoader {CacheRoot {work / "cache"}, work / "steps"};
    auto input = sourceInput(work, "Tiny");
    input.env.profile = "exit0";
    auto error = std::string {};

    const auto output = loader.build(recipeFixture(), input, error);
    check(!output.has_value());
    check(error.find("build --package Tiny") != std::string::npos);
    check(error.find("it wrote no " + (work / "steps" / "Tiny.out.json").str())
          != std::string::npos);

    // epm tells the step it reports failures itself on the command line
    // (--quiet), not in the kept input or command, which run by hand print them.
    check(eacp::Files::readFile(work / "steps" / "Tiny.in.json").find("reported")
          == std::string::npos);
    check(eacp::Files::readFile(work / "steps" / "Tiny.command").find("--quiet")
          == std::string::npos);
};

namespace
{
// Commits everything in `directory` into a git repository of its own, tagged
// v1; false without git.
bool commitAsRepository(const FilePath& directory)
{
    const auto git = findExecutable("git");

    if (!git)
        return false;

    const auto identity =
        eacp::Vector<std::string> {"-c",
                                   "user.name=epm",
                                   "-c",
                                   "user.email=epm@example.invalid",
                                   "-c",
                                   "commit.gpgsign=false",
                                   "-c",
                                   "tag.gpgsign=false"};

    for (auto arguments: {eacp::Vector<std::string> {"init", "-q"},
                          eacp::Vector<std::string> {"add", "-A"},
                          eacp::Vector<std::string> {"commit", "-q", "-m", "Tiny"},
                          eacp::Vector<std::string> {"tag", "v1"}})
    {
        auto all = identity;
        all.addFrom(arguments);

        if (!runCommand(git->str(), all, directory).ok)
            return false;
    }

    return true;
}

// The second line of a step's <name>.command: the command itself.
std::string commandOf(const FilePath& commandFile)
{
    const auto text = eacp::Files::readFile(commandFile);
    const auto lineEnd = text.find('\n');

    if (lineEnd == std::string::npos)
        return {};

    auto command = text.substr(lineEnd + 1);

    while (!command.empty() && command.back() == '\n')
        command.pop_back();

    return command;
}
} // namespace

// The recipe executable drives epm configure itself, as epm_add_recipe's
// build of it does: Tests/Recipe/epm.cpp, profile build, whose add<Tiny>
// steps (a configure hook) run in its own process, with their files kept
// in <out>/steps/ and <name>.command naming it, not epm's cached copy.
auto tRecipeConfigure = test("Recipe/main/configure") = []
{
    if (!hasRecipeCompiler() || !hasTestCMake())
        return;

    auto root = eacp::TemporaryFile {};
    const auto work = resolvedPath(root.path());
    const auto cache = CacheRoot {work / "cache"};
    auto env = tinyInput(work, cache).env;

    if (!commitAsRepository(work / "src"))
    {
        eacp::Console::print("Recipe/main/configure: skipped, no git");
        return;
    }

    const auto built = buildRecipe(recipeFixture(), cache);
    check(built.ok);

    if (!built.ok)
        return;

    // Away from the cache, as the project's own build of it is.
    const auto recipe = work / "bin" / "TestRecipe";
    check(eacp::Files::createDirectories(recipe.parentDirectory()));
    check(eacp::Files::copyFile(built.executable, recipe));
    std::filesystem::permissions(eacp::toStdPath(recipe),
                                 std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::add);

    // fmt (and on Linux uuid) from a local directory, in source mode, so
    // nothing is fetched but Tiny.
    check(eacp::Files::createDirectories(work / "fmt"));
    auto overrides = Overrides {};
    overrides.packages["fmt"].source = (work / "fmt").str();

    if (env.systemName == "Linux")
        overrides.packages["uuid"].source = (work / "fmt").str();

    overrides.packages["Tiny"].options = {{"TINY_FLAG", "ON"}};
    check(saveJsonFile(work / "env.json", env));
    check(saveJsonFile(work / "overrides.json", overrides));

    const auto out = work / "epm";
    const auto steps = out / "steps";
    const auto configure = [&]
    {
        return runCommandKeepingErrors(recipe.str(),
                                       {"configure",
                                        "--manifest",
                                        recipeFixture().str(),
                                        "--env",
                                        (work / "env.json").str(),
                                        "--overrides",
                                        (work / "overrides.json").str(),
                                        "--out",
                                        out.str(),
                                        "--cache",
                                        cache.path().str()},
                                       recipeFixture().parentDirectory());
    };

    eacp::setEnv("EPM_TEST_TINY_GIT", (work / "src").str());
    const auto first = configure();
    check(first.ok);

    if (!first.ok)
    {
        eacp::Console::printError(first.output + first.errorOutput);
        eacp::unsetEnv("EPM_TEST_TINY_GIT");
        return;
    }

    check(first.output.find("building Tiny from source into the cache")
          != std::string::npos);
    check(eacp::Files::isFile(out / "graph.json"));
    check(eacp::Files::isFile(out / "graph.cmake"));

    for (const auto* name: {"Tiny", "fmt"})
        for (const auto* suffix: {".in.json", ".out.json", ".command"})
            check(eacp::Files::isFile(steps / (std::string {name} + suffix)));

    // Run by this executable, so by hand the same.
    const auto command = commandOf(steps / "Tiny.command");
    check(command.starts_with(recipe.str() + " build --package Tiny --in "));
    check(command.find(cache.recipe().str()) == std::string::npos);

    // The add<Tiny> hook ran: its option is in the entry's fingerprint.
    const auto graph = loadGraphJson(out / "graph.json");
    check(graph.has_value());

    auto tinyPrefix = std::string {};

    if (graph)
        for (const auto& package: graph->packages)
            if (package.name == "Tiny")
                tinyPrefix = package.prefix;

    check(!tinyPrefix.empty());
    const auto entry = readEntry(FilePath {tinyPrefix});
    check(entry.has_value() && entry->fingerprint.options.at("TINY_VALUE") == "42");

    // Again: the entry is found, nothing is built.
    const auto second = configure();
    check(second.ok);
    check(second.output.find("building Tiny") == std::string::npos);
    check(loadJsonFile<BuildOutput>(steps / "Tiny.out.json")
              .value_or(BuildOutput {})
              .prefix
          == tinyPrefix);

    // A step of its own that fails fails the configure, naming the package.
    overrides.packages["Tiny"].options.clear();
    check(saveJsonFile(work / "overrides.json", overrides));
    const auto failed = configure();
    check(!failed.ok && failed.exitCode == 1);
    check(failed.errorOutput.find("TINY_FLAG is not set") != std::string::npos);
    check(failed.errorOutput.find("epm: ") != std::string::npos);
    check(failed.errorOutput.find("Tiny") != std::string::npos);
    check(!loadJsonFile<BuildOutput>(steps / "Tiny.out.json")
               .value_or(BuildOutput {})
               .ok());

    // So does its describe(), in this process too.
    env.profile = "throw";
    check(saveJsonFile(work / "env.json", env));
    const auto thrown = configure();
    check(!thrown.ok && thrown.exitCode == 1);
    check(thrown.errorOutput.find("epm: the manifest " + recipeFixture().str()
                                  + ": the recipe threw: asked to throw")
          != std::string::npos);

    eacp::unsetEnv("EPM_TEST_TINY_GIT");
};

auto tRecipeNoEntry = test("Recipe/noEntryPoint") = []
{
    if (!hasRecipeCompiler())
        return;

    auto root = eacp::TemporaryFile {};
    const auto source = root.path() / "src" / "epm.cpp";
    check(writeTextFile(source, "int notARecipe() { return 1; }\n"));

    const auto build = buildRecipe(source, CacheRoot {root.path() / "cache"});

    check(!build.ok);
    check(!eacp::Files::isFile(build.executable));
    check(build.details.find("main") != std::string::npos);
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

auto tRecipeRunMissing = test("Recipe/runMissingExecutable") = []
{
    auto root = eacp::TemporaryFile {};
    const auto run = runRecipe(root.path() / "epm-recipe", Env {});

    check(!run.project);
    check(run.error.find("no recipe executable") != std::string::npos);
};
