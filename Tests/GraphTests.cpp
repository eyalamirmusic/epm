#include <epm/Emit/GraphResponse.h>
#include <epm/Graph/Graph.h>
#include <epm/Recipe/CMakePackage.h>
#include <epm/Resolve/Resolve.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

#include <tuple>

using namespace nano;
using namespace epm;

namespace
{
// A project declaring only `name` at `tag`, from org/<name>.
Project declaring(const std::string& name, const std::string& tag)
{
    auto project = Project {};
    project.add(name, "org/" + name, tag);
    return project;
}

// What a recipe's describe() makes of an Env.
using Describe = std::function<Project(const Env&)>;

// One build step the walk asked a recipe executable to run.
struct StepCall
{
    std::string recipe;
    std::string package;
    std::string recipeName;
    std::map<std::string, std::string> deps;
    std::map<std::string, std::string> options;
    eacp::Vector<std::string> dependencies;
};

// Checkouts on disk standing in for the source cache (name@ref), and an
// in-memory ManifestLoader standing in for their epm.cpp files: a directory
// describes the project given for it, or has no epm.cpp. Build steps are
// recorded, then answered from `outputs` or `stepErrors` (by package), else
// by the default steps in this process.
struct FakeSources final : ManifestLoader
{
    FakeSources() { eacp::Files::createDirectories(root.path()); }

    // A checkout without an epm.cpp.
    void add(const std::string& name, const std::string& ref)
    {
        eacp::Files::createDirectories(directoryOf(name, ref));
    }

    void add(const std::string& name, const std::string& ref, Project project)
    {
        add(name, ref, [project](const Env&) { return project; });
    }

    void add(const std::string& name, const std::string& ref, Describe describe)
    {
        add(name, ref);
        describeAt(directoryOf(name, ref), std::move(describe));
    }

    eacp::FilePath directoryOf(const std::string& name, const std::string& ref) const
    {
        return root.path() / (name + "@" + ref);
    }

    std::string recipeOf(const std::string& name, const std::string& ref) const
    {
        return (directoryOf(name, ref) / manifestFileName).str();
    }

    void describeAt(const eacp::FilePath& directory, Describe describe)
    {
        recipes[directory.str()] = std::move(describe);
    }

    void failAt(const std::string& name, const std::string& ref, std::string error)
    {
        add(name, ref);
        failures[(root.path() / (name + "@" + ref)).str()] = std::move(error);
    }

    std::optional<Project> load(const eacp::FilePath& directory,
                                const Env& env,
                                std::string& error) override
    {
        if (const auto failure = failures.find(directory.str());
            failure != failures.end())
        {
            error = failure->second;
            return std::nullopt;
        }

        const auto found = recipes.find(directory.str());
        return found == recipes.end() ? Project {} : found->second(env);
    }

    bool hasManifest(const eacp::FilePath& directory) const override
    {
        return recipes.contains(directory.str());
    }

    std::optional<BuildOutput> build(const eacp::FilePath& recipe,
                                     const BuildInput& input,
                                     std::string& error) override
    {
        const auto& name = input.request.name;
        auto call = StepCall {recipe.str(), name, input.recipeName};
        call.deps = input.request.deps;
        call.options = input.request.options;

        for (const auto& dependency: input.dependencies)
            call.dependencies.add(dependency.name + ":" + dependency.prefix);

        steps.add(std::move(call));

        if (const auto failure = stepErrors.find(name); failure != stepErrors.end())
        {
            error = failure->second;
            return std::nullopt;
        }

        // A real step records source when asked for it.
        const auto source = input.request.mode == "source" && honoursMode;

        if (const auto output = outputs.find(name);
            output != outputs.end() && !source)
            return output->second;

        return buildInProcess(input);
    }

    // A prebuilt result for `name`'s step, with fingerprint fp-<name>.
    void prebuilt(const std::string& name)
    {
        auto output = BuildOutput {};
        output.name = name;
        output.mode = "prebuilt";
        output.sourceDirectory = "/src/" + name;
        output.prefix = "/bin/" + name;
        output.platform = "test";
        output.configs = {"Debug"};
        output.fingerprintHash = "fp-" + name;
        outputs[name] = output;
    }

    eacp::Vector<std::string> stepOrder() const
    {
        auto names = eacp::Vector<std::string> {};

        for (const auto& step: steps)
            names.add(step.package);

        return names;
    }

    const StepCall* stepOf(const std::string& name) const
    {
        return steps.findIf([&name](const StepCall& step)
                            { return step.package == name; });
    }

    SourceFetcher fetcher()
    {
        return [this](const std::string& name,
                      const std::string& url,
                      const std::string& ref)
        {
            fetched.add(name + "@" + ref);
            urls[name] = url;

            auto result = SourceResult {};
            result.snapshot.directory = root.path() / (name + "@" + ref);
            result.snapshot.commit = ref;
            result.snapshot.sourceId = name + ref;
            result.ok = eacp::Files::isDirectory(result.snapshot.directory);
            result.error = result.ok ? "" : "no " + ref + " of " + url;
            return result;
        };
    }

    // A plan of `inputs` over these sources.
    GraphPlan plan(const GraphInputs& inputs)
    {
        return planGraph(inputs, fetcher(), *this);
    }

    // A resolve of `plan` through these steps.
    GraphResult resolve(const GraphPlan& plan, const Env& env)
    {
        return resolvePlan(plan, env, CacheRoot {cacheDirectory.path()}, *this);
    }

    eacp::TemporaryFile root;
    eacp::TemporaryFile cacheDirectory;
    eacp::Vector<std::string> fetched;
    std::map<std::string, std::string> urls;
    std::map<std::string, Describe> recipes;
    std::map<std::string, std::string> failures;

    bool honoursMode = true;
    eacp::Vector<StepCall> steps;
    std::map<std::string, BuildOutput> outputs;
    std::map<std::string, std::string> stepErrors;
};

GraphInputs inputsFor(Project rootProject)
{
    auto inputs = GraphInputs {};
    inputs.project = std::move(rootProject);
    inputs.env.generator = "Ninja";
    inputs.env.buildType = "Debug";
    return inputs;
}

const PlannedPackage* planned(const GraphPlan& plan, const std::string& name)
{
    for (const auto& package: plan.packages)
        if (package.effective.request.name == name)
            return &package;

    return nullptr;
}

eacp::Vector<std::string> planOrder(const GraphPlan& plan)
{
    auto names = eacp::Vector<std::string> {};

    for (const auto& package: plan.packages)
        names.add(package.effective.request.name);

    return names;
}

bool hasNote(const GraphPlan& plan, const std::string& note)
{
    return plan.notes.contains(note);
}

MergeLayers everyLayer()
{
    auto layers = MergeLayers {};
    layers.packageOverride.mode = "source";
    layers.packageOverride.options = {{"A", "override"}};
    layers.overrides.mode = "prebuilt";
    layers.overrides.abiMatch = "any";
    layers.overrides.configs = {"Release"};

    auto root = ManifestPackage {};
    root.git = "fmtlib/fmt";
    root.tag = "11.0.2";
    root.mode = "auto";
    root.options = {{"B", "root"}, {"C", "root"}};
    layers.root = root;

    auto dependency = ManifestPackage {};
    dependency.git = "other/fmt";
    dependency.tag = "10.2.1";
    dependency.options = {{"C", "dep"}, {"D", "dep"}};
    layers.dependency = Declaration {"B", dependency};
    return layers;
}
} // namespace

auto tMergeOrder = test("Graph/merge/precedenceAndProvenance") = []
{
    auto layers = everyLayer();
    const auto all = mergeRequest("fmt", layers, Env {});

    check(all.ok);
    const auto& request = all.effective.request;
    const auto& provenance = all.effective.provenance;

    check(request.mode == "source" && provenance.at("mode") == "override:package");
    check(request.abiMatch == "any" && provenance.at("abiMatch") == "override");
    check(request.configs[0] == "Release" && provenance.at("configs") == "override");
    check(request.gitUrl == "https://github.com/fmtlib/fmt.git");
    check(provenance.at("git") == "root" && provenance.at("tag") == "root");

    check(request.options.at("A") == "override");
    check(provenance.at("options.A") == "override:package");
    check(request.options.at("B") == "root");
    check(provenance.at("options.B") == "root");
    check(request.options.at("C") == "root");
    check(provenance.at("options.C") == "root");
    check(request.options.at("D") == "dep");
    check(provenance.at("options.D") == "manifest:B");

    layers.packageOverride = {};
    layers.overrides = {};
    const auto rooted = mergeRequest("fmt", layers, Env {});
    check(rooted.effective.request.abiMatch == "compatible");
    check(rooted.effective.provenance.at("abiMatch") == "default");
    check(rooted.effective.request.configs[0] == "Release");
    check(rooted.effective.provenance.at("configs") == "default");

    layers.root->git.clear();
    const auto declared = mergeRequest("fmt", layers, Env {});
    check(declared.effective.request.mode == "auto");
    check(declared.effective.provenance.at("mode") == "root");
    check(declared.effective.request.gitUrl == "https://github.com/other/fmt.git");
    check(declared.effective.provenance.at("git") == "manifest:B");
    check(declared.effective.request.gitRef == "11.0.2");
};

auto tMergeDefaults = test("Graph/merge/defaults") = []
{
    auto layers = MergeLayers {};
    layers.root = ManifestPackage {"b", "a/b", "v1", {}, {}, {}, {}};

    const auto merged = mergeRequest("b", layers, Env {});

    check(merged.ok);
    check(merged.effective.request.mode == "auto");
    check(merged.effective.request.abiMatch == "compatible");
    check(merged.effective.request.buildMissingBinaries);
    check(merged.effective.request.configs.size() == 1);
    check(merged.effective.request.configs[0] == "Release");
    check(merged.effective.provenance.at("mode") == "default");
    check(merged.effective.provenance.at("configs") == "default");
    check(merged.effective.source.empty());
};

auto tMergeSource = test("Graph/merge/sourceOverrideMeansSourceMode") = []
{
    auto layers = everyLayer();
    layers.packageOverride.mode = "prebuilt";
    layers.packageOverride.source = "/work/fmt";
    layers.overrides.buildMissingBinaries = false;

    const auto merged = mergeRequest("fmt", layers, Env {});

    check(merged.effective.source == "/work/fmt");
    check(merged.effective.request.mode == "source");
    check(merged.effective.provenance.at("source") == "override:package");
    check(!merged.effective.request.buildMissingBinaries);
    check(merged.effective.provenance.at("buildMissingBinaries") == "override");
};

auto tMergeConfigs = test("Graph/merge/configs") = []
{
    auto single = Env {};
    single.buildType = "debug";
    auto multi = Env {};
    multi.isMultiConfig = true;

    check(*resolveConfigs({}, single) == eacp::Vector<std::string> {"Debug"});
    check(*resolveConfigs({"AUTO"}, Env {})
          == eacp::Vector<std::string> {"Release"});
    check(*resolveConfigs({"auto"}, multi)
          == eacp::Vector<std::string> {"Debug", "Release"});
    check(*resolveConfigs({"RelWithDebInfo", "debug"}, single)
          == eacp::Vector<std::string> {"Debug", "Release"});
    check(!resolveConfigs({"Profile"}, single).has_value());

    auto layers = MergeLayers {};
    layers.root = ManifestPackage {"b", "a/b", "v1", {}, {"Fast"}, {}, {}};
    const auto merged = mergeRequest("b", layers, Env {});

    check(!merged.ok);
    check(merged.error.find("Fast") != std::string::npos);
};

auto tGraphRootWins = test("Graph/walk/rootWinsAndNotes") = []
{
    auto sources = FakeSources {};
    auto a = Project {};
    setMode(setOption(a.add("C", "org/C", "v2"), "X", "dep"), "source");
    sources.add("A", "v1", a);
    sources.add("C", "v1");

    auto root = Project {};
    root.add("A", "org/A", "v1");
    setOption(root.add("C", "org/C", "v1"), "X", "root");
    const auto plan = sources.plan(inputsFor(root));

    check(plan.ok);
    check(planned(plan, "C")->effective.request.gitRef == "v1");
    check(planned(plan, "C")->effective.request.options.at("X") == "root");
    check(planned(plan, "C")->effective.request.mode == "source");
    check(planned(plan, "C")->effective.provenance.at("mode") == "manifest:A");
    check(sources.fetched == eacp::Vector<std::string> {"A@v1", "C@v1"});
    check(hasNote(plan, "C: A declares v2, root declares v1"));
    check(hasNote(plan, "C: A declares option X=dep, root declares option X=root"));

    // The root leaves mode unset, so A's mode is the winning one: no note.
    check(plan.notes.size() == 2);
    check(planned(plan, "C")->declaredBy == eacp::Vector<std::string> {"root", "A"});
};

auto tGraphFirstWins = test("Graph/walk/firstDeclarationDepthFirst") = []
{
    auto sources = FakeSources {};
    auto a = Project {};
    a.add("D", "org/D", "v1");
    a.add("E", "org/E", "fromA");
    sources.add("A", "v1", a);
    sources.add("B", "v1", declaring("E", "fromB"));
    sources.add("D", "v1", declaring("E", "fromD"));
    sources.add("E", "fromD");

    auto root = Project {};
    root.add("A", "org/A", "v1");
    root.add("B", "org/B", "v1");
    const auto plan = sources.plan(inputsFor(root));

    check(plan.ok);
    check(planned(plan, "E")->effective.request.gitRef == "fromD");
    check(planned(plan, "E")->effective.provenance.at("tag") == "manifest:D");
    check(hasNote(plan, "E: A declares fromA, D declares fromD"));
    check(hasNote(plan, "E: B declares fromB, D declares fromD"));
    check(planOrder(plan) == eacp::Vector<std::string> {"E", "D", "A", "B"});
    check(planned(plan, "A")->deps == eacp::Vector<std::string> {"D", "E"});
    check(planned(plan, "B")->deps == eacp::Vector<std::string> {"E"});
    check(planned(plan, "E")->deps.empty());
};

auto tGraphStrict = test("Graph/walk/strictTurnsNotesIntoAnError") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("C", "v2"));
    sources.add("C", "v1");

    auto root = Project {};
    root.add("A", "org/A", "v1");
    root.add("C", "org/C", "v1");
    auto inputs = inputsFor(root);

    check(sources.plan(inputs).ok);

    inputs.overrides.strict = true;
    const auto strict = sources.plan(inputs);

    check(!strict.ok);
    check(strict.error.find("C: A declares v2, root declares v1")
          != std::string::npos);

    inputs.overrides.strict = false;
    inputs.environment = [](std::string_view name)
    {
        return name == "EPM_STRICT" ? std::optional<std::string> {"1"}
                                    : std::optional<std::string> {};
    };
    check(!sources.plan(inputs).ok);
};

auto tGraphSameRepository = test("Graph/walk/sameRepositoryUnderTwoNames") = []
{
    auto sources = FakeSources {};
    sources.add("fmt", "v1");
    sources.add("fmtlib", "v1");
    sources.add("other", "v1");

    auto root = Project {};
    root.add("fmt", "fmtlib/fmt", "v1");
    root.add("fmtlib", "git@github.com:FmtLib/fmt.git", "v1");
    root.add("other", "fmtlib/other", "v1");
    const auto plan = sources.plan(inputsFor(root));

    check(plan.ok);
    check(plan.notes.size() == 1);
    check(hasNote(plan,
                  "fmt and fmtlib are the same repository (github.com/fmtlib/fmt)"));
};

auto tGraphRepositoryKey = test("Graph/repositoryKey") = []
{
    const auto key = std::string {"github.com/fmtlib/fmt"};

    check(repositoryKey("https://github.com/fmtlib/fmt.git") == key);
    check(repositoryKey("https://github.com/FmtLib/fmt/") == key);
    check(repositoryKey("git@github.com:fmtlib/fmt.git") == key);
    check(repositoryKey("ssh://git@github.com/fmtlib/fmt") == key);
    check(repositoryKey("/srv/Git/fmt.git") == "/srv/Git/fmt");
    check(repositoryKey("C:/Git/fmt.git") == "C:/Git/fmt");
};

auto tGraphSourcePropagates = test("Graph/walk/sourceForcesDependents") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("C", "v1"));
    sources.add("Top", "v1", declaring("A", "v1"));
    sources.add("B", "v1");
    sources.add("C", "v1");

    auto root = Project {};
    root.add("B", "org/B", "v1");
    root.add("Top", "org/Top", "v1");
    auto inputs = inputsFor(root);
    inputs.overrides.packages["C"].mode = "source";

    const auto plan = sources.plan(inputs);

    check(plan.ok);
    check(planned(plan, "C")->effective.request.mode == "source");
    check(planned(plan, "A")->effective.request.mode == "source");
    check(planned(plan, "A")->effective.provenance.at("mode")
          == "source dependency C");
    check(planned(plan, "Top")->effective.request.mode == "source");
    check(planned(plan, "Top")->effective.provenance.at("mode")
          == "source dependency A");
    check(planned(plan, "B")->effective.request.mode == "auto");
};

auto tGraphSourceOverride = test("Graph/walk/sourceOverrideIsReadNotFetched") = []
{
    auto sources = FakeSources {};
    sources.add("C", "v1");

    auto checkout = eacp::TemporaryFile {};
    eacp::Files::createDirectories(checkout.path());
    sources.describeAt(checkout.path(),
                       [](const Env&) { return declaring("C", "v1"); });

    auto inputs = inputsFor(declaring("A", "v9"));
    inputs.environment = [&checkout](std::string_view name)
    {
        return name == "EPM_A_SOURCE" ? std::optional {checkout.path().str()}
                                      : std::optional<std::string> {};
    };

    const auto plan = sources.plan(inputs);

    check(plan.ok);
    check(sources.fetched == eacp::Vector<std::string> {"C@v1"});
    check(planned(plan, "A")->effective.source == checkout.path().str());
    check(planned(plan, "A")->snapshot.directory == checkout.path());
    check(planned(plan, "A")->effective.request.mode == "source");
    check(planned(plan, "A")->deps == eacp::Vector<std::string> {"C"});
    check(planned(plan, "C")->effective.request.mode == "auto");
};

auto tGraphRootOptionsOnly = test("Graph/walk/rootMayOnlyAddOptions") = []
{
    auto sources = FakeSources {};
    auto a = Project {};
    setOption(a.add("C", "org/C", "v3"), "Y", "dep");
    sources.add("A", "v1", a);
    sources.add("C", "v3");

    auto root = Project {};
    root.add("A", "org/A", "v1");
    setOption(root.add("C"), "X", "root");
    const auto plan = sources.plan(inputsFor(root));

    check(plan.ok);
    const auto& request = planned(plan, "C")->effective.request;
    check(request.gitUrl == "https://github.com/org/C.git");
    check(sources.urls.at("C") == "https://github.com/org/C.git");
    check(request.gitRef == "v3");
    check(request.options.at("X") == "root");
    check(request.options.at("Y") == "dep");
    check(plan.notes.empty());

    auto orphanRoot = Project {};
    setOption(orphanRoot.add("C"), "X", "root");
    const auto orphan = sources.plan(inputsFor(orphanRoot));
    check(!orphan.ok);
    check(orphan.error.find("C:") == 0);
};

auto tGraphCycle = test("Graph/walk/cycleIsAnError") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("B", "v1"));
    sources.add("B", "v1", declaring("A", "v1"));

    const auto plan = sources.plan(inputsFor(declaring("A", "v1")));

    check(!plan.ok);
    check(plan.error == "dependency cycle: A -> B -> A");
};

auto tGraphBadInputs = test("Graph/walk/reportsBadInputs") = []
{
    auto sources = FakeSources {};
    auto a = Project {};
    a.add("C", "org/C");
    sources.add("A", "v1", a);

    const auto noTag = sources.plan(inputsFor(declaring("A", "v1")));
    check(!noTag.ok);
    check(noTag.error == "C has no tag (declared by A)");

    const auto missing = sources.plan(inputsFor(declaring("A", "v7")));
    check(!missing.ok);
    check(missing.error.find("no v7") != std::string::npos);

    auto badMode = Project {};
    setMode(badMode.add("B", "org/B", "v1"), "sometimes");
    sources.add("B", "v1");
    const auto unknownMode = sources.plan(inputsFor(badMode));
    check(!unknownMode.ok);
    check(unknownMode.error.find("sometimes") != std::string::npos);
};

auto tGraphDuplicateName = test("Graph/walk/duplicateNameIsAnError") = []
{
    auto sources = FakeSources {};
    auto a = Project {};
    a.add("C", "org/C", "v1");
    a.add("C", "org/C", "v2");
    sources.add("A", "v1", a);
    sources.add("C", "v1");

    const auto nested = sources.plan(inputsFor(declaring("A", "v1")));
    check(!nested.ok);
    check(nested.error.starts_with("the manifest of A ("));
    check(nested.error.ends_with("): C is declared twice in packages"));

    auto twice = Project {};
    twice.add("C", "org/C", "v1");
    twice.add("C", "org/C", "v1");
    const auto root = sources.plan(inputsFor(twice));
    check(!root.ok);
    check(root.error == "the root manifest: C is declared twice in packages");
    check(sources.fetched == eacp::Vector<std::string> {"A@v1"});
};

auto tGraphDeclarationOrder = test("Graph/walk/declarationOrderNotNameOrder") = []
{
    auto sources = FakeSources {};
    sources.add("zeta", "v1", declaring("P", "fromZeta"));
    sources.add("alpha", "v1", declaring("P", "fromAlpha"));
    sources.add("P", "fromZeta");

    // Name order would visit alpha first and let its P win.
    auto root = Project {};
    root.add("zeta", "org/zeta", "v1");
    root.add("alpha", "org/alpha", "v1");
    auto inputs = inputsFor(root);
    inputs.overrides.buildMissingBinaries = false;

    const auto plan = sources.plan(inputs);

    check(plan.ok);
    check(planned(plan, "P")->effective.request.gitRef == "fromZeta");
    check(planned(plan, "P")->effective.provenance.at("tag") == "manifest:zeta");
    check(planned(plan, "P")->declaredBy
          == eacp::Vector<std::string> {"zeta", "alpha"});
    check(hasNote(plan, "P: alpha declares fromAlpha, zeta declares fromZeta"));
    check(sources.fetched
          == eacp::Vector<std::string> {"zeta@v1", "P@fromZeta", "alpha@v1"});
    check(planOrder(plan) == eacp::Vector<std::string> {"P", "zeta", "alpha"});

    const auto resolved = sources.resolve(plan, inputs.env);

    check(resolved.ok);
    check(graphCMakeText(resolved.graph)
              .starts_with("set(epm_PACKAGES \"P;zeta;alpha\")\n"));
};

auto tGraphProfile = test("Graph/walk/profileReachesRecipes") = []
{
    // A recipe branching on EPM_PROFILE, as a profile is written now.
    const auto rootRecipe = [](const Env& env)
    {
        auto project = Project {};
        auto& a = project.add("A", "org/A", "v1");

        if (env.profile == "ci")
            addConfig(setMode(a, "binary-only"), "Release");

        return project;
    };

    auto sources = FakeSources {};
    sources.add("A",
                "v1",
                [](const Env& env)
                {
                    auto project = declaring("C", "v1");

                    if (env.profile == "ci")
                        setMode(project.packages[0], "prebuilt");

                    return project;
                });
    sources.add("C", "v1");

    auto inputs = inputsFor({});
    inputs.env.profile = "ci";
    inputs.project = rootRecipe(inputs.env);

    const auto plan = sources.plan(inputs);

    check(plan.ok);
    check(planned(plan, "A")->effective.request.mode == "binary-only");
    check(planned(plan, "A")->effective.request.configs[0] == "Release");
    check(planned(plan, "C")->effective.request.mode == "prebuilt");
    check(planned(plan, "C")->effective.provenance.at("mode") == "manifest:A");

    inputs.env.profile.clear();
    inputs.project = rootRecipe(inputs.env);

    const auto plain = sources.plan(inputs);

    check(plain.ok);
    check(planned(plain, "A")->effective.request.mode == "auto");
    check(planned(plain, "C")->effective.request.mode == "auto");
};

auto tGraphLoaderFails = test("Graph/walk/recipeFailureNamesThePackage") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("B", "v1"));
    sources.failAt("B", "v1", "the recipe did not compile; see compile.log");

    const auto plan = sources.plan(inputsFor(declaring("A", "v1")));

    check(!plan.ok);
    check(plan.error.starts_with("the manifest of B ("));
    check(plan.error.find("B@v1/epm.cpp): ") != std::string::npos);
    check(plan.error.ends_with("the recipe did not compile; see compile.log"));
    check(sources.fetched == eacp::Vector<std::string> {"A@v1", "B@v1"});
};

auto tGraphFallbackForcesSource =
    test("Graph/resolve/sourceFallbackForcesDependents") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("C", "v1"));
    sources.add("C", "v1");

    auto inputs = inputsFor(declaring("A", "v1"));
    inputs.overrides.buildMissingBinaries = false;

    const auto plan = sources.plan(inputs);
    check(plan.ok);
    check(planned(plan, "A")->effective.request.mode == "auto");

    const auto resolved = sources.resolve(plan, inputs.env);

    check(resolved.ok);
    check(resolved.graph.packages.size() == 2);

    const auto& c = resolved.graph.packages[0];
    const auto& a = resolved.graph.packages[1];
    check(c.name == "C" && c.mode == "source" && c.fingerprint.empty());
    check(a.name == "A" && a.mode == "source");
    check(a.request.mode == "source");
    check(a.provenance.at("mode") == "source dependency C");
    check(a.deps == eacp::Vector<std::string> {"C"});
    check(a.request.deps.empty());
    check(a.commit == "v1");
};

auto tGraphBinaryOnlyMiss = test("Graph/resolve/binaryOnlyFailsOnAMiss") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1");

    auto root = Project {};
    setMode(root.add("A", "org/A", "v1"), "binary-only");
    auto inputs = inputsFor(root);
    const auto plan = sources.plan(inputs);
    const auto resolved = sources.resolve(plan, inputs.env);

    check(!resolved.ok);
    check(resolved.error.find("A is binary-only") == 0);
};

namespace
{
// root declares A and E; A's epm.cpp declares C with steps of its own and D;
// only A has an epm.cpp.
struct StepGraph
{
    StepGraph()
    {
        auto a = Project {};
        a.add("C", "org/C", "v1").customBuild = true;
        a.add("D", "org/D", "v1");
        sources.add("A", "v1", a);
        sources.add("C", "v1");
        sources.add("D", "v1");
        sources.add("E", "v1");

        auto root = Project {};
        root.add("A", "org/A", "v1");
        root.add("E", "org/E", "v1");
        inputs = inputsFor(root);
        inputs.rootRecipe = rootRecipe;

        for (const auto* name: {"A", "C", "D", "E"})
            sources.prebuilt(name);
    }

    FakeSources sources;
    GraphInputs inputs;
    eacp::FilePath rootRecipe {"/work/app/epm.cpp"};
};
} // namespace

auto tGraphStepOwners = test("Graph/steps/ownerAndDependencyOrder") = []
{
    auto graph = StepGraph {};
    auto& sources = graph.sources;
    const auto plan = sources.plan(graph.inputs);

    check(plan.ok);
    check(plan.notes.empty());
    check(planned(plan, "C")->owner.customBuild);
    check(!planned(plan, "D")->owner.customBuild);

    const auto resolved = sources.resolve(plan, graph.inputs.env);

    check(resolved.ok);
    check(sources.stepOrder() == eacp::Vector<std::string> {"C", "D", "A", "E"});

    const auto aRecipe = sources.recipeOf("A", "v1");
    const auto ownerOf = [&](const std::string& name)
    {
        const auto* step = sources.stepOf(name);
        return step ? step->recipe + "|" + step->recipeName : std::string {};
    };

    // The declarer's recipe for C (custom steps) and D (default ones), A's
    // own for A, the root's for E.
    check(ownerOf("C") == aRecipe + "|A");
    check(ownerOf("D") == aRecipe + "|A");
    check(ownerOf("A") == aRecipe + "|A");
    check(ownerOf("E") == graph.rootRecipe.str() + "|");

    // Each step after its dependencies', given their fingerprints and entries.
    const auto* a = sources.stepOf("A");
    check(
        a
        && a->deps
               == std::map<std::string, std::string> {{"C", "fp-C"}, {"D", "fp-D"}});
    check(a
          && a->dependencies == eacp::Vector<std::string> {"C:/bin/C", "D:/bin/D"});
    check(sources.stepOf("C")->deps.empty());

    const auto* resolvedA = findPackage(resolved.graph.packages, "A");
    check(resolvedA && resolvedA->fingerprint == "fp-A");
    check(resolvedA && resolvedA->prefix == "/bin/A");
    check(resolvedA && resolvedA->request.deps.at("C") == "fp-C");
};

auto tGraphRootOptionsKeepSteps =
    test("Graph/steps/rootOptionsKeepDependencySteps") = []
{
    auto graph = StepGraph {};
    auto& sources = graph.sources;

    // The root's declaration of C, options only, wins the merge, but A's
    // custom steps own C's step, and get the root's option.
    setOption(graph.inputs.project.add("C"), "C_FLAG", "ON");
    const auto plan = sources.plan(graph.inputs);

    check(plan.ok);
    check(plan.notes.empty());

    const auto& owner = planned(plan, "C")->owner;
    check(owner.customBuild);
    check(owner.name == "A");
    check(owner.recipe.str() == sources.recipeOf("A", "v1"));

    const auto resolved = sources.resolve(plan, graph.inputs.env);
    check(resolved.ok);

    const auto* step = sources.stepOf("C");
    check(step && step->recipe == sources.recipeOf("A", "v1"));
    check(step && step->recipeName == "A");
    check(step && step->options.contains("C_FLAG")
          && step->options.at("C_FLAG") == "ON");
};

auto tGraphRootOwnsSteps = test("Graph/steps/rootCustomStepsWin") = []
{
    auto graph = StepGraph {};
    auto& sources = graph.sources;

    // The root's declaration of C with custom steps of its own wins: the
    // root's recipe runs C's step, and A's custom steps for it do not run.
    graph.inputs.project.add("C").customBuild = true;
    const auto plan = sources.plan(graph.inputs);

    check(plan.ok);
    check(plan.notes
          == eacp::Vector<std::string> {
              "C: A declares custom build steps, which do not run: root's steps "
              "run"});
    check(planned(plan, "C")->owner.customBuild);

    const auto resolved = sources.resolve(plan, graph.inputs.env);
    check(resolved.ok);
    check(sources.stepOf("C")->recipe == graph.rootRecipe.str());
    check(sources.stepOf("C")->recipeName.empty());

    graph.inputs.overrides.strict = true;
    const auto strict = sources.plan(graph.inputs);
    check(!strict.ok);
    check(strict.error.find("root's steps run") != std::string::npos);
    graph.inputs.overrides.strict = false;

    // A root without a recipe file: the default steps, in this process, for C
    // and E. C ends in source, which forces A there too; A's step still runs
    // in its own recipe, in source mode.
    graph.inputs.rootRecipe = {};
    sources.steps.clear();
    graph.inputs.overrides.buildMissingBinaries = false;
    const auto noRecipe = sources.plan(graph.inputs);
    const auto inProcess = sources.resolve(noRecipe, graph.inputs.env);
    check(inProcess.ok);
    check(sources.stepOrder() == eacp::Vector<std::string> {"D", "A"});

    const auto* a = findPackage(inProcess.graph.packages, "A");
    check(a && a->mode == "source");
};

auto tGraphFirstCustomStepsOwn = test("Graph/steps/firstCustomStepsOwn") = []
{
    // root declares A then B; both declare C, without an epm.cpp of its own.
    const auto planWith = [](bool aCustom, bool bCustom)
    {
        auto sources = FakeSources {};
        auto a = Project {};
        a.add("C", "org/C", "v1").customBuild = aCustom;
        auto b = Project {};
        b.add("C", "org/C", "v1").customBuild = bCustom;
        sources.add("A", "v1", a);
        sources.add("B", "v1", b);
        sources.add("C", "v1");

        auto root = Project {};
        root.add("A", "org/A", "v1");
        root.add("B", "org/B", "v1");
        auto plan = sources.plan(inputsFor(root));
        const auto owner = plan.ok ? planned(plan, "C")->owner : StepOwner {};
        return std::tuple {plan.ok, plan.notes, owner.name, owner.customBuild};
    };

    // A's declaration wins the merge, but only B's has custom steps.
    const auto [bOk, bNotes, bOwner, bCustom] = planWith(false, true);
    check(bOk && bNotes.empty() && bOwner == "B" && bCustom);

    // Both have: the first, A's, runs, and B's are noted.
    const auto [bothOk, bothNotes, bothOwner, bothCustom] = planWith(true, true);
    check(bothOk && bothOwner == "A" && bothCustom);
    check(bothNotes
          == eacp::Vector<std::string> {
              "C: B declares custom build steps, which do not run: A's steps run"});

    // Neither: the winning declaration's recipe, with the default steps.
    const auto [noneOk, noneNotes, noneOwner, noneCustom] = planWith(false, false);
    check(noneOk && noneNotes.empty() && noneOwner == "A" && !noneCustom);
};

auto tGraphDependencyNamedRoot = test("Graph/steps/aDependencyNamedRoot") = []
{
    // A package called "root", whose epm.cpp declares X with custom steps:
    // X's step runs in that package's recipe, not the root project's.
    auto sources = FakeSources {};
    auto rootPackage = Project {};
    rootPackage.add("X", "org/X", "v1").customBuild = true;
    sources.add("root", "v1", rootPackage);
    sources.add("X", "v1");

    auto inputs = inputsFor(declaring("root", "v1"));
    inputs.rootRecipe = eacp::FilePath {"/work/app/epm.cpp"};
    const auto plan = sources.plan(inputs);

    check(plan.ok);
    check(plan.notes.empty());

    const auto& owner = planned(plan, "X")->owner;
    check(owner.name == "root");
    check(owner.recipe.str() == sources.recipeOf("root", "v1"));
    check(owner.customBuild);

    // The root project's own custom steps for X win, and the package's are
    // noted as not running.
    inputs.project.add("X").customBuild = true;
    const auto rootWins = sources.plan(inputs);
    check(rootWins.ok);
    check(planned(rootWins, "X")->owner.recipe == inputs.rootRecipe);
    check(planned(rootWins, "X")->owner.name.empty());
    check(rootWins.notes
          == eacp::Vector<std::string> {
              "X: root declares custom build steps, which do not run: root's steps "
              "run"});
};

auto tGraphCustomStepsOwnRecipe =
    test("Graph/steps/customStepsForAPackageWithItsOwnRecipe") = []
{
    auto graph = StepGraph {};
    auto& sources = graph.sources;
    sources.add("C", "v1", Project {});

    const auto plan = sources.plan(graph.inputs);

    check(!plan.ok);
    check(plan.error
          == "A declares custom build steps for C, which has its own "
             "epm.cpp");

    // Without add<T>, C's own epm.cpp runs its step.
    graph.inputs.project.packages.clear();
    graph.inputs.project.add("C", "org/C", "v1");
    const auto own = sources.plan(graph.inputs);
    check(own.ok);
    check(planned(own, "C")->owner.recipe.str() == sources.recipeOf("C", "v1"));
    check(planned(own, "C")->owner.name == "C");
};

auto tGraphSourceStepInOwner = test("Graph/steps/sourceStepRunsInOwner") = []
{
    auto graph = StepGraph {};
    auto& sources = graph.sources;
    graph.inputs.overrides.buildMissingBinaries = false;
    graph.inputs.project.packages.clear();
    graph.inputs.project.add("D", "org/D", "v1");
    graph.inputs.project.add("E", "org/E", "v1");

    // A source package's step runs in its owner's executable too, for the
    // options its hooks set.
    graph.inputs.overrides.mode = "source";
    const auto source =
        sources.resolve(sources.plan(graph.inputs), graph.inputs.env);
    check(source.ok);
    check(sources.stepOrder() == eacp::Vector<std::string> {"D", "E"});
    check(source.graph.packages.size() == 2
          && source.graph.packages[1].mode == "source");

    // A step that records prebuilt for a source package fails.
    sources.honoursMode = false;
    sources.steps.clear();
    const auto prebuilt =
        sources.resolve(sources.plan(graph.inputs), graph.inputs.env);
    check(!prebuilt.ok);
    check(prebuilt.error
          == "the build step of D recorded prebuilt, but the package is in "
             "source mode: end build() with useSource() when spec().mode is "
             "source");
};

auto tGraphStepOptions = test("Graph/steps/optionsFromTheStep") = []
{
    auto graph = StepGraph {};
    auto& sources = graph.sources;
    graph.inputs.overrides.mode = "source";

    // D's step records the options its hook set, E's none: graph.cmake hands
    // D's to its add_subdirectory, and E keeps the request's.
    setOption(graph.inputs.project.packages[1], "E_FLAG", "ON");
    sources.honoursMode = false;

    for (auto& [name, output]: sources.outputs)
        output.mode = "source";

    sources.outputs["D"].options = {{"D_HOOK", "ON"}, {"D_TYPED", "1"}};

    const auto resolved =
        sources.resolve(sources.plan(graph.inputs), graph.inputs.env);
    check(resolved.ok);

    const auto* d = findPackage(resolved.graph.packages, "D");
    const auto* e = findPackage(resolved.graph.packages, "E");
    check(d && d->options.at("D_HOOK") == "ON");
    check(d && d->request.options.empty());
    check(e && e->options.at("E_FLAG") == "ON");

    const auto text = graphCMakeText(resolved.graph);
    check(text.find("set(epm_D_OPTIONS \"D_HOOK=ON;D_TYPED=1\")\n")
          != std::string::npos);
    check(text.find("set(epm_E_OPTIONS \"E_FLAG=ON\")\n") != std::string::npos);
};

auto tGraphStepFails = test("Graph/steps/failureNamesThePackage") = []
{
    auto graph = StepGraph {};
    auto& sources = graph.sources;
    sources.stepErrors["D"] = "recipe build --package D failed: exit code 1\n"
                              "-- epm: D: building D (Debug)\n"
                              "error: D.cpp:1: nope";

    const auto plan = sources.plan(graph.inputs);
    const auto failed = sources.resolve(plan, graph.inputs.env);

    check(!failed.ok);
    check(failed.error.starts_with("the build step of D (in the recipe "
                                   + sources.recipeOf("A", "v1") + "): "));
    check(failed.error.ends_with("building D (Debug)\nerror: D.cpp:1: nope"));
    check(sources.stepOrder() == eacp::Vector<std::string> {"C", "D"});

    // A step that reports its failure: its error, and its details above it.
    sources.stepErrors.clear();
    sources.steps.clear();
    sources.outputs["C"] = BuildOutput {};
    sources.outputs["C"].name = "C";
    sources.outputs["C"].error = "C is binary-only, but the cache has none";
    sources.outputs["C"].details = "last lines";

    const auto reported = sources.resolve(plan, graph.inputs.env);
    check(!reported.ok);
    check(reported.error == "C is binary-only, but the cache has none");
    check(reported.details == "last lines");
    check(sources.stepOrder() == eacp::Vector<std::string> {"C"});
};

auto tGraphDecide = test("Graph/decide/binaryOnly") = []
{
    auto request = Request {};
    request.name = "A";
    request.gitUrl = "/srv/A";
    request.gitRef = "v1";
    request.configs = {"Release"};
    request.mode = "binary-only";

    check(decide(request, true) == Decision::useBinary);
    check(decide(request, false) == Decision::missingBinary);
    check(!requestError(request).has_value());
};

namespace
{
ResolvedGraph sampleGraph()
{
    auto fmt = ResolvedPackage {};
    fmt.name = "fmt";
    fmt.mode = "prebuilt";
    fmt.sourceDir = "/cache/src/fmt/abc";
    fmt.request.options = {{"FMT_TEST", "OFF"}, {"LIST", "a;b"}};
    fmt.prefix = "/cache/bin/fmt/macos-arm64/0123456789ab";
    fmt.platform = "macos-arm64";
    fmt.configs = {"Debug", "Release"};
    fmt.fingerprint = "0123456789abcdef";

    auto b = ResolvedPackage {};
    b.name = "B";
    b.mode = "source";
    b.sourceDir = "/work/B";
    b.deps = {"fmt"};

    auto graph = ResolvedGraph {};
    graph.packages = {fmt, b};
    graph.notes = {"fmt: B declares 10.2.1, root declares 11.0.2",
                   "fmt: B declares configs Debug;Release, root leaves it unset"};
    return graph;
}
} // namespace

auto tGraphCMake = test("GraphResponse/cmakeText") = []
{
    const auto expected =
        "set(epm_PACKAGES \"fmt;B\")\n"
        "set(epm_fmt_MODE prebuilt)\n"
        "set(epm_fmt_SOURCE_DIR \"/cache/src/fmt/abc\")\n"
        "set(epm_fmt_DEPS \"\")\n"
        "set(epm_fmt_OPTIONS \"FMT_TEST=OFF;LIST=a\\\\;b\")\n"
        "set(epm_fmt_PREFIX \"/cache/bin/fmt/macos-arm64/0123456789ab\")\n"
        "set(epm_fmt_TARGETS_FILE \"${epm_fmt_PREFIX}/epm-targets.cmake\")\n"
        "set(epm_fmt_PLATFORM \"macos-arm64\")\n"
        "set(epm_fmt_CONFIGS \"Debug;Release\")\n"
        "set(epm_fmt_NOTE \"\")\n"
        "set(epm_B_MODE source)\n"
        "set(epm_B_SOURCE_DIR \"/work/B\")\n"
        "set(epm_B_DEPS \"fmt\")\n"
        "set(epm_B_OPTIONS \"\")\n"
        "set(epm_NOTES \"fmt: B declares 10.2.1, root declares 11.0.2;"
        "fmt: B declares configs Debug\\\\;Release, root leaves it unset\")\n";

    check(graphCMakeText(sampleGraph()) == expected);
    check(graphCMakeText({}) == "set(epm_PACKAGES \"\")\nset(epm_NOTES \"\")\n");
};

auto tGraphJsonRoundTrip = test("GraphResponse/jsonRoundTrip") = []
{
    auto directory = eacp::TemporaryFile {};
    const auto graph = sampleGraph();

    check(writeGraphJson(directory.path() / "graph.json", graph));

    const auto loaded = loadGraphJson(directory.path() / "graph.json");

    check(loaded.has_value());
    check(loaded->schema == 1);
    check(loaded->notes == graph.notes);
    check(graphCMakeText(*loaded) == graphCMakeText(graph));

    const auto text = eacp::Files::readFile(directory.path() / "graph.json");
    check(text.find("\"provenance\"") != std::string::npos);
    check(text.find("\"fingerprint\": \"0123456789abcdef\"") != std::string::npos);
};

auto tGraphDependencyClosure = test("Graph/dependencyClosure") = []
{
    const auto package = [](std::string name, eacp::Vector<std::string> deps)
    {
        auto resolved = ResolvedPackage {};
        resolved.name = std::move(name);
        resolved.mode = "prebuilt";
        resolved.deps = std::move(deps);
        return resolved;
    };

    auto graph = ResolvedGraph {};
    graph.packages = {package("Leaf", {}),
                      package("Other", {}),
                      package("Mid", {"Leaf"}),
                      package("Top", {"Mid"})};
    graph.notes = {"a note"};

    const auto names = [](const ResolvedGraph& closure)
    {
        auto result = eacp::Vector<std::string> {};

        for (const auto& resolved: closure.packages)
            result.add(resolved.name);

        return result;
    };

    check(names(dependencyClosure(graph, {"Leaf"}))
          == eacp::Vector<std::string> {"Leaf"});
    check(names(dependencyClosure(graph, {"Mid"}))
          == eacp::Vector<std::string> {"Leaf", "Mid"});
    check(names(dependencyClosure(graph, {"Top", "Other"}))
          == eacp::Vector<std::string> {"Leaf", "Other", "Mid", "Top"});
    check(dependencyClosure(graph, {}).packages.empty());
    check(dependencyClosure(graph, {"Mid"}).notes.empty());
};
