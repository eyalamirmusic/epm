#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <map>
#include <optional>
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
    eacp::Vector<std::string> configs;

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

// Why the request cannot be resolved, or nullopt when it is well formed.
std::optional<std::string> requestError(const Request& request);

enum class Decision
{
    useSource,
    useBinary,
    buildBinary,
    missingBinary
};

// MODE source: always source. A cached binary is used whenever one exists.
// Otherwise prebuilt builds it, binary-only fails, and auto builds it only
// when buildMissingBinaries allows, else falls back to source.
Decision decide(const Request& request, bool binaryExists);
} // namespace epm
