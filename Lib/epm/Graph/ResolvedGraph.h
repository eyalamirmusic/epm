#pragma once

#include <epm/Spec/Request.h>

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <map>
#include <string>

namespace epm
{
// One package of graph.json.
struct ResolvedPackage
{
    std::string name;

    // The effective request, deps holding each direct dependency's
    // fingerprint hash when the package is prebuilt.
    Request request;

    // "prebuilt" or "source".
    std::string mode;
    std::string sourceDir;

    // The override checkout (EPM_<Name>_SOURCE); empty for a fetched one.
    std::string source;

    // Empty for an override checkout.
    std::string commit;

    // Prebuilt only.
    std::string prefix;
    std::string targetsFile;
    std::string platform;
    eacp::Vector<std::string> configs;

    // Prebuilt only: fingerprintId of the entry used, full hex.
    std::string fingerprint;
    std::string note;

    // Direct dependencies, names.
    eacp::Vector<std::string> deps;

    // Every manifest declaring the package, "root" first, then in walk order.
    eacp::Vector<std::string> declaredBy;

    // Which source set each effective value (Graph/Merge.h).
    std::map<std::string, std::string> provenance;

    // The options the build step built or configured the package with
    // (BuildOutput::options, a hook's included), else request.options: what
    // graph.cmake's OPTIONS hands a source package.
    std::map<std::string, std::string> options;

    // options, or request.options for a graph.json written without them.
    const std::map<std::string, std::string>& effectiveOptions() const
    {
        return options.empty() ? request.options : options;
    }

    MIRO_REFLECT(name,
                 request,
                 options,
                 mode,
                 sourceDir,
                 source,
                 commit,
                 prefix,
                 targetsFile,
                 platform,
                 configs,
                 fingerprint,
                 note,
                 deps,
                 declaredBy,
                 provenance)
};

// graph.json (Docs/Graph.md §6): what `epm configure` resolved.
struct ResolvedGraph
{
    int schema = 1;

    // In dependency order: every package after its dependencies.
    eacp::Vector<ResolvedPackage> packages;
    eacp::Vector<std::string> notes;

    MIRO_REFLECT(schema, packages, notes)
};
} // namespace epm
