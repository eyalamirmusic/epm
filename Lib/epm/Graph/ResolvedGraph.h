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
    EA::Vector<std::string> configs;

    // Prebuilt only: fingerprintId of the entry used, full hex.
    std::string fingerprint;
    std::string note;

    // Direct dependencies, names.
    EA::Vector<std::string> deps;

    // Every manifest declaring the package, "root" first, then in walk order.
    EA::Vector<std::string> declaredBy;

    // Which source set each effective value (Graph/Merge.h).
    std::map<std::string, std::string> provenance;

    MIRO_REFLECT(name,
                 request,
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
    EA::Vector<ResolvedPackage> packages;
    EA::Vector<std::string> notes;

    MIRO_REFLECT(schema, packages, notes)
};
} // namespace epm
