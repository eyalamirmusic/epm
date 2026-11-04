#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <map>
#include <string>

namespace epm
{
// The request schema this binary reads; epm.cmake writes the same number.
constexpr auto requestSchema = 2;

// requests/<name>.json: one epm_add_package call.
struct Request
{
    int schema = requestSchema;
    std::string name;
    std::string gitUrl;
    std::string gitRef;

    // "auto", "prebuilt", "binary-only" or "source".
    std::string mode = "auto";

    // Sorted by key, which the fingerprint's canonical form relies on.
    std::map<std::string, std::string> options;

    bool buildMissingBinaries = true;

    // EPM_CONFIGS as epm.cmake resolved it: the configurations the consumer
    // needs, e.g. ["Release"] or ["Debug", "Release"].
    EA::Vector<std::string> configs;

    // EPM_ABI_MATCH: "compatible", "strict" or "any".
    std::string abiMatch = "compatible";

    // Direct dependency name -> the fingerprint hash of the entry it resolved
    // to, filled by the graph resolver. Part of the fingerprint, so a changed
    // dependency changes the entry of everything linking it.
    std::map<std::string, std::string> deps;

    MIRO_REFLECT(schema,
                 name,
                 gitUrl,
                 gitRef,
                 mode,
                 options,
                 buildMissingBinaries,
                 configs,
                 abiMatch,
                 deps)
};
} // namespace epm
