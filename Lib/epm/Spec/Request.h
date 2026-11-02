#pragma once

#include <Miro/Reflect.h>

#include <map>
#include <string>

namespace epm
{
// requests/<name>.json: one epm_add_package call.
struct Request
{
    int schema = 1;
    std::string name;
    std::string gitUrl;
    std::string gitRef;

    // "auto", "prebuilt" or "source".
    std::string mode = "auto";

    // Sorted by key, which the binary key's canonical form relies on.
    std::map<std::string, std::string> options;

    bool buildMissingBinaries = true;

    MIRO_REFLECT(schema, name, gitUrl, gitRef, mode, options, buildMissingBinaries)
};
} // namespace epm
