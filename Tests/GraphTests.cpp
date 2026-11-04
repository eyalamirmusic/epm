#include <epm/Emit/GraphResponse.h>
#include <epm/Graph/Graph.h>
#include <epm/Resolve/Resolve.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
// Not checked, so planGraph's own check of the root can be tested.
Manifest manifestFrom(std::string_view json)
{
    return Miro::createFromJSON<Manifest>(Miro::Json::parse(json));
}

// A manifest declaring only `name` at `tag`, from org/<name>.
std::string declaring(const std::string& name, const std::string& tag)
{
    return R"({ "packages": [ { "name": ")" + name + R"(", "git": "org/)" + name
           + R"(", "tag": ")" + tag + R"(" } ] })";
}

// Checkouts on disk standing in for the source cache: name@ref holds the
// epm.json given for it.
struct FakeSources
{
    FakeSources() { eacp::Files::createDirectories(root.path()); }

    void add(const std::string& name,
             const std::string& ref,
             std::string_view manifest = {})
    {
        const auto directory = root.path() / (name + "@" + ref);
        eacp::Files::createDirectories(directory);

        if (!manifest.empty())
            writeTextFile(directory / "epm.json", manifest);
    }

    SourceFetcher fetcher()
    {
        return
            [this](const std::string& name, const EA::Vector<Requirement>& declared)
        {
            auto requirements = declared;

            for (auto& requirement: requirements)
                requirement.commit = requirement.requestedRef;

            auto result = LockedSource {};
            auto selection = selectRequirement(requirements);

            if (selection.isConflict)
            {
                result.error = describeRequirementConflict(name, requirements);
                return result;
            }

            const auto& ref = selection.selected.requestedRef;
            const auto& url = selection.selected.url;
            fetched.add(name + "@" + ref);
            urls[name] = url;

            result.selected = selection.selected;
            result.snapshot.directory = root.path() / (name + "@" + ref);
            result.snapshot.commit = ref;
            result.snapshot.hash = name + ref;
            result.ok = eacp::Files::isDirectory(result.snapshot.directory);
            result.error = result.ok ? "" : "no " + ref + " of " + url;
            return result;
        };
    }

    eacp::TemporaryFile root;
    EA::Vector<std::string> fetched;
    std::map<std::string, std::string> urls;
};

GraphInputs inputsFor(std::string_view rootManifest)
{
    auto inputs = GraphInputs {};
    inputs.manifest = manifestFrom(rootManifest);
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

EA::Vector<std::string> planOrder(const GraphPlan& plan)
{
    auto names = EA::Vector<std::string> {};

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
    layers.profileName = "ci";
    layers.profile.mode = "binary-only";
    layers.profile.abiMatch = "strict";
    layers.profile.configs = {"Debug"};
    layers.profile.packages.add(
        ProfilePackage {"other", {{"A", "other"}}, {}, "source", {}});

    auto& fmt = layers.profile.packages.create();
    fmt.name = "fmt";
    fmt.options = {{"A", "profile"}, {"B", "profile"}};
    fmt.abiMatch = "compatible";

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
    check(request.options.at("B") == "profile");
    check(provenance.at("options.B") == "profile:ci:package");
    check(request.options.at("C") == "root");
    check(provenance.at("options.C") == "root");
    check(request.options.at("D") == "dep");
    check(provenance.at("options.D") == "manifest:B");

    layers.packageOverride = {};
    layers.overrides = {};
    const auto profiled = mergeRequest("fmt", layers, Env {});
    check(profiled.effective.request.mode == "binary-only");
    check(profiled.effective.provenance.at("mode") == "profile:ci");
    check(profiled.effective.request.abiMatch == "compatible");
    check(profiled.effective.provenance.at("abiMatch") == "profile:ci:package");
    check(profiled.effective.request.configs[0] == "Debug");

    layers.profile = {};
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

    check(*resolveConfigs({}, single) == EA::Vector<std::string> {"Debug"});
    check(*resolveConfigs({"AUTO"}, Env {}) == EA::Vector<std::string> {"Release"});
    check(*resolveConfigs({"auto"}, multi)
          == EA::Vector<std::string> {"Debug", "Release"});
    check(*resolveConfigs({"RelWithDebInfo", "debug"}, single)
          == EA::Vector<std::string> {"Debug", "Release"});
    check(!resolveConfigs({"Profile"}, single).has_value());

    auto layers = MergeLayers {};
    layers.root = ManifestPackage {"b", "a/b", "v1", {}, {"Fast"}, {}, {}};
    const auto merged = mergeRequest("b", layers, Env {});

    check(!merged.ok);
    check(merged.error.find("Fast") != std::string::npos);
};

auto tGraphHighestTag = test("Graph/walk/highestTagWinsRootOptionsWin") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", R"({ "packages": [ { "name": "C", "git": "org/C",
        "tag": "v2", "options": { "X": "dep" }, "mode": "source" } ] })");
    sources.add("C", "v2");

    const auto plan = planGraph(inputsFor(R"({ "packages": [
            { "name": "A", "git": "org/A", "tag": "v1" },
            { "name": "C", "git": "org/C", "tag": "v1",
              "options": { "X": "root" } } ] })"),
                                sources.fetcher());

    check(plan.ok);
    check(planned(plan, "C")->effective.request.gitRef == "v2");
    check(planned(plan, "C")->effective.provenance.at("tag") == "A");
    check(planned(plan, "C")->effective.request.options.at("X") == "root");
    check(planned(plan, "C")->effective.request.mode == "source");
    check(planned(plan, "C")->effective.provenance.at("mode") == "manifest:A");
    check(sources.fetched == EA::Vector<std::string> {"A@v1", "C@v2"});
    check(hasNote(plan, "C: A declares option X=dep, root declares option X=root"));

    // Tags are selected, not merged, so they make no note.
    check(plan.notes.size() == 1);
    check(planned(plan, "C")->declaredBy == EA::Vector<std::string> {"root", "A"});
};

auto tGraphDepthFirst = test("Graph/walk/depthFirstOrder") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", R"({ "packages": [
        { "name": "D", "git": "org/D", "tag": "v1" },
        { "name": "E", "git": "org/E", "tag": "e1" } ] })");
    sources.add(
        "B",
        "v1",
        R"({ "packages": [ { "name": "E", "git": "org/E", "tag": "e1" } ] })");
    sources.add(
        "D",
        "v1",
        R"({ "packages": [ { "name": "E", "git": "org/E", "tag": "e1" } ] })");
    sources.add("E", "e1");

    const auto plan = planGraph(inputsFor(R"({ "packages": [
            { "name": "A", "git": "org/A", "tag": "v1" },
            { "name": "B", "git": "org/B", "tag": "v1" } ] })"),
                                sources.fetcher());

    check(plan.ok);
    check(planned(plan, "E")->effective.request.gitRef == "e1");
    check(planOrder(plan) == EA::Vector<std::string> {"E", "D", "A", "B"});
    check(planned(plan, "A")->deps == EA::Vector<std::string> {"D", "E"});
    check(planned(plan, "B")->deps == EA::Vector<std::string> {"E"});
    check(planned(plan, "E")->deps.empty());
};

auto tGraphConflict = test("Graph/walk/differentRefsConflict") = []
{
    auto sources = FakeSources {};
    sources.add("zeta", "v1", declaring("P", "fromZeta"));
    sources.add("alpha", "v1", declaring("P", "fromAlpha"));
    sources.add("P", "fromZeta");

    const auto plan = planGraph(inputsFor(R"({ "packages": [
            { "name": "zeta", "git": "org/zeta", "tag": "v1" },
            { "name": "alpha", "git": "org/alpha", "tag": "v1" } ] })"),
                                sources.fetcher());

    check(!plan.ok);
    check(plan.error.find("disagree on P") != std::string::npos);
    check(plan.error.find("zeta asks for") != std::string::npos);
    check(plan.error.find("alpha asks for") != std::string::npos);
    check(plan.error.find("epm.json") != std::string::npos);
};

auto tGraphStrict = test("Graph/walk/strictTurnsNotesIntoAnError") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", R"({ "packages": [ { "name": "C", "git": "org/C",
        "tag": "v1", "options": { "X": "dep" } } ] })");
    sources.add("C", "v1");

    auto inputs = inputsFor(R"({ "packages": [
        { "name": "A", "git": "org/A", "tag": "v1" },
        { "name": "C", "git": "org/C", "tag": "v1", "options": { "X": "root" } } ] })");

    check(planGraph(inputs, sources.fetcher()).ok);

    inputs.overrides.strict = true;
    const auto strict = planGraph(inputs, sources.fetcher());

    check(!strict.ok);
    check(
        strict.error.find("C: A declares option X=dep, root declares option X=root")
        != std::string::npos);

    inputs.overrides.strict = false;
    inputs.environment = [](std::string_view name)
    {
        return name == "EPM_STRICT" ? std::optional<std::string> {"1"}
                                    : std::optional<std::string> {};
    };
    check(!planGraph(inputs, sources.fetcher()).ok);
};

auto tGraphSameRepository = test("Graph/walk/sameRepositoryUnderTwoNames") = []
{
    auto sources = FakeSources {};
    sources.add("fmt", "v1");
    sources.add("fmtlib", "v1");
    sources.add("other", "v1");

    const auto plan = planGraph(inputsFor(R"({ "packages": [
            { "name": "fmt", "git": "fmtlib/fmt", "tag": "v1" },
            { "name": "fmtlib", "git": "git@github.com:FmtLib/fmt.git", "tag": "v1" },
            { "name": "other", "git": "fmtlib/other", "tag": "v1" } ] })"),
                                sources.fetcher());

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
};

auto tGraphSourcePropagates = test("Graph/walk/sourceForcesDependents") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("C", "v1"));
    sources.add("Top", "v1", declaring("A", "v1"));
    sources.add("B", "v1");
    sources.add("C", "v1");

    auto inputs = inputsFor(R"({ "packages": [
        { "name": "B", "git": "org/B", "tag": "v1" },
        { "name": "Top", "git": "org/Top", "tag": "v1" } ] })");
    inputs.overrides.packages["C"].mode = "source";

    const auto plan = planGraph(inputs, sources.fetcher());

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
    writeTextFile(checkout.path() / "epm.json", declaring("C", "v1"));

    auto inputs = inputsFor(declaring("A", "v9"));
    inputs.environment = [&checkout](std::string_view name)
    {
        return name == "EPM_A_SOURCE" ? std::optional {checkout.path().str()}
                                      : std::optional<std::string> {};
    };

    const auto plan = planGraph(inputs, sources.fetcher());

    check(plan.ok);
    check(sources.fetched == EA::Vector<std::string> {"C@v1"});
    check(planned(plan, "A")->effective.source == checkout.path().str());
    check(planned(plan, "A")->snapshot.directory == checkout.path());
    check(planned(plan, "A")->effective.request.mode == "source");
    check(planned(plan, "A")->deps == EA::Vector<std::string> {"C"});
    check(planned(plan, "C")->effective.request.mode == "auto");
};

auto tGraphRootOptionsOnly = test("Graph/walk/rootMayOnlyAddOptions") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", R"({ "packages": [ { "name": "C", "git": "org/C",
        "tag": "v3", "options": { "Y": "dep" } } ] })");
    sources.add("C", "v3");

    const auto plan = planGraph(inputsFor(R"({ "packages": [
            { "name": "A", "git": "org/A", "tag": "v1" },
            { "name": "C", "options": { "X": "root" } } ] })"),
                                sources.fetcher());

    check(plan.ok);
    const auto& request = planned(plan, "C")->effective.request;
    check(request.gitUrl == "https://github.com/org/C.git");
    check(sources.urls.at("C") == "https://github.com/org/C.git");
    check(request.gitRef == "v3");
    check(request.options.at("X") == "root");
    check(request.options.at("Y") == "dep");
    check(plan.notes.empty());

    const auto orphan = planGraph(
        inputsFor(
            R"({ "packages": [ { "name": "C", "options": { "X": "root" } } ] })"),
        sources.fetcher());
    check(!orphan.ok);
    check(orphan.error.find("C:") == 0);
};

auto tGraphCycle = test("Graph/walk/cycleIsAnError") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("B", "v1"));
    sources.add("B", "v1", declaring("A", "v1"));

    const auto plan = planGraph(inputsFor(declaring("A", "v1")), sources.fetcher());

    check(!plan.ok);
    check(plan.error == "dependency cycle: A -> B -> A");
};

auto tGraphBadInputs = test("Graph/walk/reportsBadInputs") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", R"({ "packages": [ { "name": "C", "git": "org/C" } ] })");

    const auto noTag = planGraph(inputsFor(declaring("A", "v1")), sources.fetcher());
    check(!noTag.ok);
    check(noTag.error == "C has no git and tag (declared by A)");

    const auto missing =
        planGraph(inputsFor(declaring("A", "v7")), sources.fetcher());
    check(!missing.ok);
    check(missing.error.find("no v7") != std::string::npos);

    auto badMode = inputsFor(R"({ "packages": [ { "name": "B", "git": "org/B",
        "tag": "v1", "mode": "sometimes" } ] })");
    sources.add("B", "v1");
    const auto unknownMode = planGraph(badMode, sources.fetcher());
    check(!unknownMode.ok);
    check(unknownMode.error.find("sometimes") != std::string::npos);

    auto badProfile = inputsFor(R"({ "packages": [] })");
    badProfile.overrides.profile = "ci";
    check(!planGraph(badProfile, sources.fetcher()).ok);
};

auto tGraphDuplicateName = test("Graph/walk/duplicateNameIsAnError") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", R"({ "packages": [
        { "name": "C", "git": "org/C", "tag": "v1" },
        { "name": "C", "git": "org/C", "tag": "v2" } ] })");
    sources.add("C", "v1");

    const auto nested =
        planGraph(inputsFor(declaring("A", "v1")), sources.fetcher());
    check(!nested.ok);
    check(nested.error.starts_with("the manifest of A ("));
    check(nested.error.ends_with("): C is declared twice in packages"));

    const auto root = planGraph(inputsFor(R"({ "packages": [
        { "name": "C", "git": "org/C", "tag": "v1" },
        { "name": "C", "git": "org/C", "tag": "v1" } ] })"),
                                sources.fetcher());
    check(!root.ok);
    check(root.error == "the root manifest: C is declared twice in packages");
    check(sources.fetched == EA::Vector<std::string> {"A@v1"});
};

auto tGraphDeclarationOrder = test("Graph/walk/declarationOrderNotNameOrder") = []
{
    auto sources = FakeSources {};
    sources.add("zeta", "v1", declaring("P", "v1"));
    sources.add("alpha", "v1", declaring("P", "v1"));
    sources.add("P", "v1");

    // Name order would visit alpha first.
    auto inputs = inputsFor(R"({ "packages": [
            { "name": "zeta", "git": "org/zeta", "tag": "v1" },
            { "name": "alpha", "git": "org/alpha", "tag": "v1" } ] })");
    inputs.overrides.buildMissingBinaries = false;

    const auto plan = planGraph(inputs, sources.fetcher());

    check(plan.ok);
    check(planned(plan, "P")->declaredBy
          == EA::Vector<std::string> {"zeta", "alpha"});
    // alpha's declaration, met after P was fetched, takes part in the
    // selection again.
    check(sources.fetched
          == EA::Vector<std::string> {"zeta@v1", "P@v1", "alpha@v1", "P@v1"});
    check(planOrder(plan) == EA::Vector<std::string> {"P", "zeta", "alpha"});

    auto cacheDirectory = eacp::TemporaryFile {};
    const auto resolved =
        resolvePlan(plan, inputs.env, CacheRoot {cacheDirectory.path()}, {});

    check(resolved.ok);
    check(graphCMakeText(resolved.graph)
              .starts_with("set(epm_PACKAGES \"P;zeta;alpha\")\n"));
};

auto tGraphProfile = test("Graph/walk/profileApplies") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("C", "v1"));
    sources.add("C", "v1");

    auto inputs = inputsFor(R"({
        "packages": [ { "name": "A", "git": "org/A", "tag": "v1" } ],
        "profiles": { "ci": { "mode": "binary-only", "configs": ["Release"],
            "packages": [ { "name": "C", "mode": "prebuilt" } ] } } })");
    inputs.overrides.profile = "ci";

    const auto plan = planGraph(inputs, sources.fetcher());

    check(plan.ok);
    check(planned(plan, "A")->effective.request.mode == "binary-only");
    check(planned(plan, "A")->effective.request.configs[0] == "Release");
    check(planned(plan, "C")->effective.request.mode == "prebuilt");
    check(planned(plan, "C")->effective.provenance.at("mode")
          == "profile:ci:package");
};

auto tGraphFallbackForcesSource =
    test("Graph/resolve/sourceFallbackForcesDependents") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1", declaring("C", "v1"));
    sources.add("C", "v1");

    auto inputs = inputsFor(declaring("A", "v1"));
    inputs.overrides.buildMissingBinaries = false;

    const auto plan = planGraph(inputs, sources.fetcher());
    check(plan.ok);
    check(planned(plan, "A")->effective.request.mode == "auto");

    auto cacheDirectory = eacp::TemporaryFile {};
    const auto resolved =
        resolvePlan(plan, inputs.env, CacheRoot {cacheDirectory.path()}, {});

    check(resolved.ok);
    check(resolved.graph.packages.size() == 2);

    const auto& c = resolved.graph.packages[0];
    const auto& a = resolved.graph.packages[1];
    check(c.name == "C" && c.mode == "source" && c.fingerprint.empty());
    check(a.name == "A" && a.mode == "source");
    check(a.request.mode == "source");
    check(a.provenance.at("mode") == "source dependency C");
    check(a.deps == EA::Vector<std::string> {"C"});
    check(a.request.deps.empty());
    check(a.commit == "v1");
};

auto tGraphBinaryOnlyMiss = test("Graph/resolve/binaryOnlyFailsOnAMiss") = []
{
    auto sources = FakeSources {};
    sources.add("A", "v1");

    auto inputs = inputsFor(R"({ "packages": [ { "name": "A", "git": "org/A",
        "tag": "v1", "mode": "binary-only" } ] })");
    const auto plan = planGraph(inputs, sources.fetcher());

    auto cacheDirectory = eacp::TemporaryFile {};
    const auto resolved =
        resolvePlan(plan, inputs.env, CacheRoot {cacheDirectory.path()}, {});

    check(!resolved.ok);
    check(resolved.error.find("A is binary-only") == 0);
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
    const auto package = [](std::string name, EA::Vector<std::string> deps)
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
        auto result = EA::Vector<std::string> {};

        for (const auto& resolved: closure.packages)
            result.add(resolved.name);

        return result;
    };

    check(names(dependencyClosure(graph, {"Leaf"}))
          == EA::Vector<std::string> {"Leaf"});
    check(names(dependencyClosure(graph, {"Mid"}))
          == EA::Vector<std::string> {"Leaf", "Mid"});
    check(names(dependencyClosure(graph, {"Top", "Other"}))
          == EA::Vector<std::string> {"Leaf", "Other", "Mid", "Top"});
    check(dependencyClosure(graph, {}).packages.empty());
    check(dependencyClosure(graph, {"Mid"}).notes.empty());
};
