#include "Graph.h"

#include <epm/Build/Configs.h>
#include <epm/Emit/GraphResponse.h>
#include <epm/Recipe/CMakePackage.h>
#include <epm/Resolve/Resolve.h>
#include <epm/Spec/Manifest.h>
#include <epm/Util/Log.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/Strings.h>

#include <set>

namespace epm
{
using namespace eacp;

namespace
{
struct Node
{
    std::optional<ManifestPackage> root;

    // Declarations by dependencies, in walk order: the first one wins.
    Vector<Declaration> declarations;
    bool visiting = false;
    bool done = false;
    SourceSnapshot snapshot;
    bool hasManifest = false;
    Vector<std::string> deps;
};

std::optional<Declaration> firstDependency(const Node& node)
{
    if (node.declarations.empty())
        return std::nullopt;

    return node.declarations.front();
}

// Every declaration of the package in merge order: the root's first, then
// the dependencies' in walk order. The first one wins the merge.
Vector<Declaration> declarationsOf(const Node& node)
{
    auto declarations = Vector<Declaration> {};

    if (node.root)
        declarations.add({rootDeclarer, *node.root, true});

    for (const auto& declaration: node.declarations)
        declarations.add(declaration);

    return declarations;
}

bool sameDeclarer(const Declaration& a, const Declaration& b)
{
    return a.isRoot == b.isRoot && a.declarer == b.declarer;
}

// The first declaration with custom steps (add<T>) for the package.
std::optional<Declaration> firstCustomSteps(const Node& node)
{
    const auto declarations = declarationsOf(node);

    if (const auto* custom =
            declarations.findIf([](const Declaration& declaration)
                                { return declaration.package.customBuild; }))
        return *custom;

    return std::nullopt;
}

// Whose recipe runs the build step of a package without an epm.cpp of its
// own: the winning declaration's when it has custom steps, else the first
// declaration's that has, else the winning one's (default steps). Options
// reach the step through the merged request whoever owns it, so a root that
// only tunes options keeps a dependency's custom steps.
Declaration stepsDeclaration(const Node& node)
{
    if (auto custom = firstCustomSteps(node))
        return std::move(*custom);

    return declarationsOf(node).front();
}

// Custom steps that never run: declared beside another declaration's custom
// steps, which own the step. (With an epm.cpp of its own, any custom steps
// are an error.)
Vector<std::string> unusedStepsNotes(const std::string& name, const Node& node)
{
    auto notes = Vector<std::string> {};

    if (node.hasManifest)
        return notes;

    // A project declares a package once, so its declarer names a declaration.
    const auto owner = stepsDeclaration(node);

    for (const auto& declaration: declarationsOf(node))
        if (declaration.package.customBuild && !sameDeclarer(declaration, owner))
            notes.add(name + ": " + declaration.declarer
                      + " declares custom build steps, which do not run: "
                      + owner.declarer + "'s steps run");

    return notes;
}

Vector<std::string> declarersOf(const Node& node)
{
    auto declarers = Vector<std::string> {};

    if (node.root)
        declarers.add(rootDeclarer);

    for (const auto& declaration: node.declarations)
        declarers.add(declaration.declarer);

    return declarers;
}

// The dependency in `sourceNames` that forces a package into source.
std::optional<std::string> sourceDependency(const Vector<std::string>& deps,
                                            const Vector<std::string>& sourceNames)
{
    for (const auto& dep: deps)
        if (sourceNames.contains(dep))
            return dep;

    return std::nullopt;
}

void forceSourceDependents(Vector<PlannedPackage>& packages)
{
    auto sourceNames = Vector<std::string> {};

    for (auto& package: packages)
    {
        auto& request = package.effective.request;

        if (request.mode != "source")
        {
            if (auto dep = sourceDependency(package.deps, sourceNames))
            {
                request.mode = "source";
                package.effective.provenance["mode"] = "source dependency " + *dep;
            }
        }

        if (request.mode == "source")
            sourceNames.add(request.name);
    }
}

std::string configsText(const Vector<std::string>& configs)
{
    if (configs.empty())
        return {};

    if (isAutoConfigs(configs))
        return "auto";

    const auto built = buildConfigsFor(configs);
    return joinList(built.empty() ? configs : built, ";");
}

// A field of the winning declaration and who declared it.
struct WinningValue
{
    std::string value;
    std::string declarer;
};

using FieldText = std::function<std::string(const ManifestPackage&)>;

WinningValue winningValue(const Node& node, const FieldText& field)
{
    if (node.root && !field(*node.root).empty())
        return {field(*node.root), rootDeclarer};

    const auto first = firstDependency(node);

    if (first && !field(first->package).empty())
        return {field(first->package), first->declarer};

    return {{}, node.root || !first ? std::string {rootDeclarer} : first->declarer};
}

std::string noteText(const std::string& name,
                     const std::string& later,
                     const std::string& label,
                     const std::string& laterValue,
                     const WinningValue& winner)
{
    const auto describe = [&](const std::string& value)
    { return label.empty() ? value : label + " " + value; };

    return name + ": " + later + " declares " + describe(laterValue) + ", "
           + winner.declarer
           + (winner.value.empty() ? " leaves it unset"
                                   : " declares " + describe(winner.value));
}

// Rule 4: every later declaration asking for something the winning one does
// not give, field by field (only the fields the later one sets).
Vector<std::string> declarationNotes(const std::string& name, const Node& node)
{
    auto notes = Vector<std::string> {};
    const auto& declarations = node.declarations;

    struct Field
    {
        std::string label;
        FieldText text;
        std::function<bool(const std::string&, const std::string&)> same;
    };

    const auto equal = [](const std::string& a, const std::string& b)
    { return a == b; };
    const auto fields = Vector<Field> {
        {"git",
         [](const ManifestPackage& p) { return expandGitUrl(p.git); },
         [](const std::string& a, const std::string& b)
         { return repositoryKey(a) == repositoryKey(b); }},
        {"", [](const ManifestPackage& p) { return p.tag; }, equal},
        {"configs",
         [](const ManifestPackage& p) { return configsText(p.configs); },
         equal},
        {"mode", [](const ManifestPackage& p) { return p.mode; }, equal},
        {"abiMatch", [](const ManifestPackage& p) { return p.abiMatch; }, equal},
    };

    for (auto index = node.root ? 0 : 1; index < declarations.size(); ++index)
    {
        const auto& later = declarations[index];

        for (const auto& field: fields)
        {
            const auto value = field.text(later.package);
            const auto winner = winningValue(node, field.text);

            if (!value.empty() && !field.same(value, winner.value))
                notes.add(
                    noteText(name, later.declarer, field.label, value, winner));
        }

        for (const auto& [key, value]: later.package.options)
        {
            const auto option = [&key](const ManifestPackage& p)
            {
                const auto found = p.options.find(key);
                return found == p.options.end() ? std::string {}
                                                : key + "=" + found->second;
            };
            const auto winner = winningValue(node, option);
            const auto wanted = key + "=" + value;

            if (wanted != winner.value)
                notes.add(noteText(name, later.declarer, "option", wanted, winner));
        }
    }

    return notes;
}

// Rule 5.
Vector<std::string> sameRepositoryNotes(const Vector<PlannedPackage>& packages)
{
    auto keys = Vector<std::string> {};
    auto names = std::map<std::string, Vector<std::string>> {};

    for (const auto& package: packages)
    {
        const auto& request = package.effective.request;

        if (request.gitUrl.empty())
            continue;

        const auto key = repositoryKey(request.gitUrl);
        keys.addIfNotThere(key);
        names[key].add(request.name);
    }

    auto notes = Vector<std::string> {};

    for (const auto& key: keys)
        if (names[key].size() > 1)
            notes.add(joinList(names[key], " and ") + " are the same repository ("
                      + key + ")");

    return notes;
}

class GraphWalk
{
public:
    GraphWalk(const GraphInputs& inputsToUse,
              const SourceFetcher& fetchToUse,
              ManifestLoader& loaderToUse)
        : inputs(inputsToUse)
        , fetch(fetchToUse)
        , loader(loaderToUse)
    {
    }

    GraphPlan run()
    {
        auto plan = GraphPlan {};

        if (auto problem = projectError(inputs.project))
            return failed(plan, "the root manifest: " + *problem);

        auto rootNames = Vector<std::string> {};

        for (const auto& package: inputs.project.packages)
        {
            rootNames.add(package.name);
            nodes[package.name].root = package;
        }

        overrides = withEnvironment(inputs.overrides, rootNames, inputs.environment);

        for (const auto& name: rootNames)
            if (startsAtRoot(name) && !visit(name))
                return failed(plan, error);

        for (const auto& name: rootNames)
            if (!nodes[name].done)
                return failed(plan,
                              name
                                  + ": the root declares it without git and tag, "
                                    "and no dependency declares it");

        for (const auto& name: order)
        {
            auto planned = plannedPackage(name);

            if (!planned)
                return failed(plan, error);

            plan.packages.add(std::move(*planned));
        }

        forceSourceDependents(plan.packages);

        for (const auto& name: order)
        {
            for (auto& note: declarationNotes(name, nodes[name]))
                plan.notes.add(std::move(note));

            for (auto& note: unusedStepsNotes(name, nodes[name]))
                plan.notes.add(std::move(note));
        }

        for (auto& note: sameRepositoryNotes(plan.packages))
            plan.notes.add(std::move(note));

        if (overrides.strict && !plan.notes.empty())
            return failed(plan,
                          "EPM_STRICT is on and the graph has notes:\n  "
                              + joinList(plan.notes, "\n  "));

        plan.ok = true;
        return plan;
    }

private:
    GraphPlan& failed(GraphPlan& plan, const std::string& message)
    {
        plan.ok = false;
        plan.error = message;
        return plan;
    }

    bool fail(std::string message)
    {
        error = std::move(message);
        return false;
    }

    std::optional<PackageOverride> packageOverride(const std::string& name) const
    {
        const auto found = overrides.packages.find(name);

        if (found == overrides.packages.end())
            return std::nullopt;

        return found->second;
    }

    std::string sourceOverride(const std::string& name) const
    {
        const auto package = packageOverride(name);
        return package ? package->source : std::string {};
    }

    bool startsAtRoot(const std::string& name)
    {
        const auto& root = nodes[name].root;

        return !sourceOverride(name).empty()
               || (!root->git.empty() && !root->tag.empty());
    }

    bool visit(const std::string& name)
    {
        auto& node = nodes[name];

        if (node.done)
            return true;

        if (node.visiting)
            return fail("dependency cycle: " + joinList(stack, " -> ") + " -> "
                        + name);

        node.visiting = true;
        stack.add(name);
        overrides =
            withEnvironment(std::move(overrides), {name}, inputs.environment);

        if (!takeSource(name, node) || !visitDependencies(name, node))
            return false;

        node.visiting = false;
        node.done = true;
        stack.resize(stack.size() - 1);
        order.add(name);
        return true;
    }

    bool takeSource(const std::string& name, Node& node)
    {
        if (const auto source = sourceOverride(name); !source.empty())
        {
            if (!Files::isDirectory(FilePath {source}))
                return fail("EPM_" + name + "_SOURCE: " + source
                            + " is not a directory");

            Log::step("epm: " + name + " from " + source);
            node.snapshot.directory = FilePath {source};
            return true;
        }

        const auto git = winningValue(node, [](auto& p) { return p.git; }).value;
        const auto tag = winningValue(node, [](auto& p) { return p.tag; }).value;

        if (git.empty() || tag.empty())
            return fail(name + " has no " + (git.empty() ? "git" : "tag")
                        + " (declared by " + joinList(declarersOf(node), ", ")
                        + ")");

        const auto fetched = fetch(name, expandGitUrl(git), tag);

        if (!fetched.ok)
            return fail(fetched.error);

        node.snapshot = fetched.snapshot;
        return true;
    }

    // Declaring and visiting one dependency at a time keeps the walk depth
    // first: a declaration inside an earlier dependency's subtree comes
    // before a later sibling's.
    bool visitDependencies(const std::string& name, Node& node)
    {
        const auto manifestOf = [&]
        {
            const auto path = node.snapshot.directory / manifestFileName;
            return "the manifest of " + name + " (" + path.str() + "): ";
        };

        node.hasManifest = loader.hasManifest(node.snapshot.directory);

        auto loadError = std::string {};
        const auto project =
            loader.load(node.snapshot.directory, inputs.env, loadError);

        if (!project)
            return fail(manifestOf() + loadError);

        if (auto problem = projectError(*project))
            return fail(manifestOf() + *problem);

        for (const auto& package: project->packages)
        {
            const auto& dep = package.name;

            if (dep == name)
                continue;

            nodes[dep].declarations.add({name, package});
            node.deps.add(dep);

            if (!visit(dep))
                return false;
        }

        return true;
    }

    std::optional<PlannedPackage> plannedPackage(const std::string& name)
    {
        const auto& node = nodes[name];

        auto layers = MergeLayers {};
        layers.packageOverride = packageOverride(name).value_or(PackageOverride {});
        layers.overrides = overrides;
        layers.root = node.root;
        layers.dependency = firstDependency(node);

        auto merged = mergeRequest(name, layers, inputs.env);

        if (!merged.ok)
        {
            fail(merged.error);
            return std::nullopt;
        }

        if (merged.effective.source.empty())
        {
            if (auto requestProblem = requestError(merged.effective.request))
            {
                fail(*requestProblem);
                return std::nullopt;
            }
        }

        if (merged.effective.request.mode != "source")
        {
            if (auto linkProblem =
                    configLinkProblem(inputs.env, merged.effective.request.configs))
            {
                fail(name + ": " + *linkProblem);
                return std::nullopt;
            }
        }

        const auto owner = stepOwner(name, node);

        if (!owner)
            return std::nullopt;

        return PlannedPackage {std::move(merged.effective),
                               node.snapshot,
                               node.deps,
                               declarersOf(node),
                               *owner};
    }

    // Always a package's own epm.cpp when it has one, even without add<T>
    // anywhere: an override of its Recipe's build() shows nowhere else.
    std::optional<StepOwner> stepOwner(const std::string& name, const Node& node)
    {
        if (node.hasManifest)
        {
            if (const auto custom = firstCustomSteps(node))
            {
                fail(custom->declarer + " declares custom build steps for " + name
                     + ", which has its own epm.cpp");
                return std::nullopt;
            }

            return StepOwner {name, node.snapshot.directory / manifestFileName};
        }

        const auto owner = stepsDeclaration(node);
        const auto customBuild = owner.package.customBuild;

        if (owner.isRoot)
            return StepOwner {{}, inputs.rootRecipe, customBuild};

        const auto& declarer = nodes.at(owner.declarer);
        return StepOwner {owner.declarer,
                          declarer.snapshot.directory / manifestFileName,
                          customBuild};
    }

    const GraphInputs& inputs;
    const SourceFetcher& fetch;
    ManifestLoader& loader;
    Overrides overrides;
    std::map<std::string, Node> nodes;
    Vector<std::string> order;
    Vector<std::string> stack;
    std::string error;
};

ResolvedPackage resolvedPackage(const PlannedPackage& planned,
                                Request request,
                                Provenance provenance,
                                const ResolveResult& resolved)
{
    const auto& response = resolved.response;

    auto package = ResolvedPackage {};
    package.name = request.name;
    package.request = std::move(request);
    package.mode = response.mode;
    package.sourceDir = response.sourceDirectory.str();
    package.source = planned.effective.source;
    package.commit = planned.snapshot.commit;
    package.deps = planned.deps;
    package.declaredBy = planned.declaredBy;
    package.provenance = std::move(provenance);
    package.options =
        resolved.options.empty() ? package.request.options : resolved.options;

    if (response.mode != "prebuilt")
        return package;

    package.prefix = response.prefix.str();
    package.targetsFile = (response.prefix / "epm-targets.cmake").str();
    package.platform = response.platform;
    package.configs = response.configs;
    package.fingerprint = resolved.fingerprintHash;
    package.note = response.note;
    return package;
}

// The build step's view of `deps`, every one already in `graph`.
Vector<PackageDependency> packageDependencies(const ResolvedGraph& graph,
                                              const Vector<std::string>& deps)
{
    auto dependencies = Vector<PackageDependency> {};

    for (const auto& dep: deps)
        if (const auto* package = findPackage(graph.packages, dep))
            dependencies.add({package->name,
                              package->mode,
                              package->fingerprint,
                              package->prefix,
                              package->targetsFile,
                              package->configs});

    return dependencies;
}

ResolveResult stepFailure(std::string error)
{
    auto result = ResolveResult {};
    result.error = std::move(error);
    return result;
}

// A step asked for source that recorded a prebuilt entry would hand the
// consumer binaries built without its source dependencies.
ResolveResult checkedMode(const BuildInput& input, ResolveResult result)
{
    if (result.ok && input.request.mode == "source"
        && result.response.mode != "source")
        return stepFailure("the build step of " + input.request.name + " recorded "
                           + result.response.mode
                           + ", but the package is in source mode: end build() "
                             "with useSource() when spec().mode is source");

    return result;
}

// Runs the package's build step in its owner's recipe executable, source
// packages too (their hooks set the options the consumer's add_subdirectory
// gets); the default steps in this process for an owner without a recipe.
ResolveResult
    runStep(const PlannedPackage& planned, BuildInput input, ManifestLoader& steps)
{
    const auto& owner = planned.owner;
    const auto& name = input.request.name;

    if (owner.recipe.empty())
        return resolveResultFor(buildInProcess(input));

    input.recipeName = owner.name;
    input.customBuild = owner.customBuild;

    // This process holds no PackageLock here, and must not: the step takes
    // the package's lock itself (Package::cached), and eacp's IPC::Lock is
    // not re-entrant, so it would wait out the timeout on one held here.
    auto error = std::string {};
    const auto output = steps.build(owner.recipe, input, error);

    if (!output)
        return stepFailure("the build step of " + name + " (in the recipe "
                           + owner.recipe.str() + "): " + error);

    return checkedMode(input, resolveResultFor(*output));
}
} // namespace

std::string repositoryKey(std::string_view url)
{
    auto key = std::string {url};
    const auto isLocal = isLocalRepository(key);
    const auto scheme = key.find("://");

    if (scheme != std::string::npos)
        key = key.substr(scheme + 3);
    else if (const auto colon = key.find(':');
             colon != std::string::npos && !isLocal)
        key[colon] = '/';

    if (const auto at = key.find('@'); at != std::string::npos && at < key.find('/'))
        key = key.substr(at + 1);

    while (key.ends_with('/'))
        key.pop_back();

    if (key.ends_with(".git"))
        key.resize(key.size() - 4);

    return isLocal ? key : Strings::toLower(key);
}

GraphPlan planGraph(const GraphInputs& inputs,
                    const SourceFetcher& fetch,
                    ManifestLoader& loader)
{
    return GraphWalk {inputs, fetch, loader}.run();
}

ResolvedGraph dependencyClosure(const ResolvedGraph& graph,
                                const Vector<std::string>& deps)
{
    auto closure = std::set<std::string> {};
    auto pending = deps;

    while (!pending.empty())
    {
        const auto name = pending.back();
        pending.pop_back();

        if (!closure.insert(name).second)
            continue;

        for (const auto& package: graph.packages)
            if (package.name == name)
                pending.addFrom(package.deps);
    }

    auto result = ResolvedGraph {};

    for (const auto& package: graph.packages)
        if (closure.contains(package.name))
            result.packages.add(package);

    return result;
}

GraphResult resolvePlan(const GraphPlan& plan,
                        const Env& env,
                        const CacheRoot& cache,
                        ManifestLoader& steps)
{
    auto result = GraphResult {};
    auto fingerprints = std::map<std::string, std::string> {};
    auto sourceNames = Vector<std::string> {};

    for (const auto& planned: plan.packages)
    {
        auto request = planned.effective.request;
        auto provenance = planned.effective.provenance;

        if (request.mode != "source")
        {
            if (auto dep = sourceDependency(planned.deps, sourceNames))
            {
                request.mode = "source";
                provenance["mode"] = "source dependency " + *dep;
            }
        }

        auto dependencyGraph = std::string {};

        if (request.mode != "source" && !planned.deps.empty())
        {
            for (const auto& dep: planned.deps)
                request.deps[dep] = fingerprints[dep];

            dependencyGraph =
                graphCMakeText(dependencyClosure(result.graph, planned.deps));
        }

        const auto resolved =
            runStep(planned,
                    buildInputFor(request,
                                  env,
                                  cache,
                                  planned.snapshot,
                                  dependencyGraph,
                                  packageDependencies(result.graph, planned.deps)),
                    steps);

        if (!resolved.ok)
        {
            result.error = resolved.error;
            result.details = resolved.details;
            return result;
        }

        if (resolved.response.mode == "prebuilt")
            fingerprints[request.name] = resolved.fingerprintHash;
        else
            sourceNames.add(request.name);

        result.graph.packages.add(resolvedPackage(
            planned, std::move(request), std::move(provenance), resolved));
    }

    result.graph.notes = plan.notes;
    result.ok = true;
    return result;
}

GraphResult configureGraph(const GraphInputs& inputs,
                           const CacheRoot& cache,
                           ManifestLoader& loader)
{
    const auto fetch = [&cache](const std::string& name,
                                const std::string& url,
                                const std::string& ref)
    { return fetchSource(cache, name, url, ref); };

    const auto plan = planGraph(inputs, fetch, loader);

    if (!plan.ok)
    {
        auto result = GraphResult {};
        result.error = plan.error;
        return result;
    }

    return resolvePlan(plan, inputs.env, cache, loader);
}
} // namespace epm
