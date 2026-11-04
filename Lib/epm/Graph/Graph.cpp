#include "Graph.h"

#include <epm/Build/Configs.h>
#include <epm/Emit/GraphResponse.h>
#include <epm/Resolve/Resolve.h>
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
constexpr auto manifestFileName = "epm.json";

struct Node
{
    std::optional<ManifestPackage> root;

    // Declarations by dependencies, in walk order: the first one wins.
    EA::Vector<Declaration> declarations;
    bool visiting = false;
    bool done = false;
    SourceSnapshot snapshot;
    EA::Vector<std::string> deps;

    // The requirement minimal version selection chose, and how many
    // declarations the choice was made from.
    Requirement selected;
    int selectedFromDeclarations = 0;
};

// Every declaration of the node that names a source: the root's, then the
// dependencies' in walk order.
EA::Vector<Requirement> requirementsOf(const Node& node)
{
    auto requirements = EA::Vector<Requirement> {};

    if (node.root && !node.root->git.empty() && !node.root->tag.empty())
        requirements.add(
            {rootDeclarer, expandGitUrl(node.root->git), node.root->tag, {}});

    for (const auto& declaration: node.declarations)
    {
        const auto& package = declaration.package;

        if (!package.git.empty() && !package.tag.empty())
            requirements.add(
                {declaration.declarer, expandGitUrl(package.git), package.tag, {}});
    }

    return requirements;
}

std::optional<Declaration> firstDependency(const Node& node)
{
    if (node.declarations.empty())
        return std::nullopt;

    return node.declarations.front();
}

EA::Vector<std::string> declarersOf(const Node& node)
{
    auto declarers = EA::Vector<std::string> {};

    if (node.root)
        declarers.add(rootDeclarer);

    for (const auto& declaration: node.declarations)
        declarers.add(declaration.declarer);

    return declarers;
}

// The dependency in `sourceNames` that forces a package into source.
std::optional<std::string>
    sourceDependency(const EA::Vector<std::string>& deps,
                     const EA::Vector<std::string>& sourceNames)
{
    for (const auto& dep: deps)
        if (sourceNames.contains(dep))
            return dep;

    return std::nullopt;
}

void forceSourceDependents(EA::Vector<PlannedPackage>& packages)
{
    auto sourceNames = EA::Vector<std::string> {};

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

std::string configsText(const EA::Vector<std::string>& configs)
{
    if (configs.empty())
        return {};

    if (isAutoConfigs(configs))
        return "auto";

    const auto built = buildConfigsFor(configs);
    return eacp::Strings::join(built.empty() ? configs : built, ";");
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
EA::Vector<std::string> declarationNotes(const std::string& name, const Node& node)
{
    auto notes = EA::Vector<std::string> {};
    const auto& declarations = node.declarations;

    struct Field
    {
        std::string label;
        FieldText text;
        std::function<bool(const std::string&, const std::string&)> same;
    };

    const auto equal = [](const std::string& a, const std::string& b)
    { return a == b; };
    // Git and tag are selected, not merged, so they make no note.
    const auto fields = EA::Vector<Field> {
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
EA::Vector<std::string>
    sameRepositoryNotes(const EA::Vector<PlannedPackage>& packages)
{
    auto keys = EA::Vector<std::string> {};
    auto names = std::map<std::string, EA::Vector<std::string>> {};

    for (const auto& package: packages)
    {
        const auto& request = package.effective.request;

        if (request.gitUrl.empty())
            continue;

        const auto key = repositoryKey(request.gitUrl);
        keys.addIfNotThere(key);
        names[key].add(request.name);
    }

    auto notes = EA::Vector<std::string> {};

    for (const auto& key: keys)
        if (names[key].size() > 1)
            notes.add(eacp::Strings::join(names[key], " and ")
                      + " are the same repository (" + key + ")");

    return notes;
}

class GraphWalk
{
public:
    GraphWalk(const GraphInputs& inputsToUse, const SourceFetcher& fetchToUse)
        : inputs(inputsToUse)
        , fetch(fetchToUse)
    {
    }

    GraphPlan run()
    {
        auto plan = GraphPlan {};

        if (auto problem = manifestError(inputs.manifest))
            return failed(plan, "the root manifest: " + *problem);

        auto rootNames = EA::Vector<std::string> {};

        for (const auto& package: inputs.manifest.packages)
        {
            if (package.system)
            {
                addSystemPackage(package.name);
                continue;
            }

            rootNames.add(package.name);
            nodes[package.name].root = package;
        }

        overrides = withEnvironment(inputs.overrides, rootNames, inputs.environment);

        const auto profile = selectProfile(
            inputs.manifest, overrides.profile, inputs.manifestDirectory);

        if (!profile.ok)
            return failed(plan, profile.error);

        for (const auto& name: rootNames)
            if (startsAtRoot(name) && !visit(name))
                return failed(plan, error);

        for (const auto& name: order)
            if (!reselect(name))
                return failed(plan, error);

        for (const auto& name: rootNames)
            if (!nodes[name].done)
                return failed(plan,
                              name
                                  + ": the root declares it without git and tag, "
                                    "and no dependency declares it");

        for (const auto& name: order)
        {
            auto planned = plannedPackage(name, profile.profile);

            if (!planned)
                return failed(plan, error);

            plan.packages.add(std::move(*planned));
        }

        forceSourceDependents(plan.packages);
        plan.systemPackages = systemPackages;

        for (const auto& name: order)
            for (auto& note: declarationNotes(name, nodes[name]))
                plan.notes.add(std::move(note));

        for (auto& note: sameRepositoryNotes(plan.packages))
            plan.notes.add(std::move(note));

        if (overrides.strict && !plan.notes.empty())
            return failed(plan,
                          "EPM_STRICT is on and the graph has notes:\n  "
                              + eacp::Strings::join(plan.notes, "\n  "));

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
            return fail("dependency cycle: " + eacp::Strings::join(stack, " -> ")
                        + " -> " + name);

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

        const auto requirements = requirementsOf(node);

        if (requirements.empty())
            return fail(name + " has no git and tag (declared by "
                        + eacp::Strings::join(declarersOf(node), ", ") + ")");

        const auto fetched = fetch(name, requirements);

        if (!fetched.ok)
            return fail(fetched.error);

        node.snapshot = fetched.snapshot;
        node.selected = fetched.selected;
        node.selectedFromDeclarations = node.declarations.size();
        return true;
    }

    // A declaration met after the package was fetched takes part in the
    // selection too. When it changes the choice, the lock now records it, and
    // the next configure fetches the new choice from the start.
    bool reselect(const std::string& name)
    {
        auto& node = nodes[name];

        if (!sourceOverride(name).empty()
            || node.declarations.size() == node.selectedFromDeclarations)
            return true;

        const auto fetched = fetch(name, requirementsOf(node));

        if (!fetched.ok)
            return fail(fetched.error);

        if (fetched.snapshot.hash != node.snapshot.hash)
            return fail(name + " was fetched at '" + node.selected.requestedRef
                        + "', and then " + fetched.selected.requestedBy
                        + " asked for '" + fetched.selected.requestedRef
                        + "', which minimal version selection takes instead. The "
                          "lock now holds it; configure again.");

        return true;
    }

    // Declaring and visiting one dependency at a time keeps the walk depth
    // first: a declaration inside an earlier dependency's subtree comes
    // before a later sibling's.
    void addSystemPackage(const std::string& name)
    {
        if (!systemPackages.contains(name))
            systemPackages.add(name);
    }

    bool visitDependencies(const std::string& name, Node& node)
    {
        const auto path = node.snapshot.directory / manifestFileName;

        if (!Files::isFile(path))
            return true;

        const auto nested = loadManifest(path);

        if (!nested.ok)
            return fail("the manifest of " + name + " (" + path.str()
                        + "): " + nested.error);

        for (const auto& package: nested.manifest.packages)
        {
            const auto& dep = package.name;

            if (dep == name)
                continue;

            if (package.system)
            {
                addSystemPackage(dep);
                continue;
            }

            nodes[dep].declarations.add({name, package});
            node.deps.add(dep);

            if (!visit(dep))
                return false;
        }

        return true;
    }

    std::optional<PlannedPackage> plannedPackage(const std::string& name,
                                                 const Profile& profile)
    {
        const auto& node = nodes[name];

        auto layers = MergeLayers {};
        layers.packageOverride = packageOverride(name).value_or(PackageOverride {});
        layers.overrides = overrides;
        layers.profileName = overrides.profile;
        layers.profile = profile;
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
            merged.effective.request.gitUrl = node.selected.url;
            merged.effective.request.gitRef = node.selected.requestedRef;
            merged.effective.provenance["git"] = node.selected.requestedBy;
            merged.effective.provenance["tag"] = node.selected.requestedBy;

            if (auto requestProblem = requestError(merged.effective.request))
            {
                fail(*requestProblem);
                return std::nullopt;
            }
        }

        return PlannedPackage {std::move(merged.effective),
                               node.snapshot,
                               node.deps,
                               declarersOf(node)};
    }

    const GraphInputs& inputs;
    const SourceFetcher& fetch;
    Overrides overrides;
    std::map<std::string, Node> nodes;
    EA::Vector<std::string> order;
    EA::Vector<std::string> systemPackages;
    EA::Vector<std::string> stack;
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
} // namespace

std::string repositoryKey(std::string_view url)
{
    auto key = std::string {url};
    const auto isLocal = key.starts_with('/');
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

GraphPlan planGraph(const GraphInputs& inputs, const SourceFetcher& fetch)
{
    return GraphWalk {inputs, fetch}.run();
}

ResolvedGraph dependencyClosure(const ResolvedGraph& graph,
                                const EA::Vector<std::string>& deps)
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
                        const LockForChildBuilds& childLock)
{
    auto result = GraphResult {};
    auto fingerprints = std::map<std::string, std::string> {};
    auto sourceNames = EA::Vector<std::string> {};

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

        const auto resolved = resolveSnapshot(
            request, env, cache, planned.snapshot, childLock, dependencyGraph);

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
    result.graph.systemPackages = plan.systemPackages;
    result.ok = true;
    return result;
}

GraphResult configureGraph(const GraphInputs& inputs,
                           const CacheRoot& cache,
                           const SourceFetcher& fetch,
                           const LockForChildBuilds& childLock)
{
    const auto plan = planGraph(inputs, fetch);

    if (!plan.ok)
    {
        auto result = GraphResult {};
        result.error = plan.error;
        return result;
    }

    return resolvePlan(plan, inputs.env, cache, childLock);
}
} // namespace epm
