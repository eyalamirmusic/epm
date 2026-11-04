#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// EPM_<Name>_MODE, _SOURCE, _CONFIGS and _OPTIONS.
struct PackageOverride
{
    std::string mode;

    // A local checkout used instead of fetching; makes the package source.
    std::string source;
    EA::Vector<std::string> configs;
    std::map<std::string, std::string> options;

    MIRO_REFLECT(mode, source, configs, options)
};

// overrides.json (Docs/Graph.md §3), written by get_epm.cmake from the CMake
// variables it sees. Unset strings and lists are empty.
struct Overrides
{
    int schema = 1;
    std::string profile;
    std::string mode;

    // ["auto"] asks for the PoC's auto rule explicitly.
    EA::Vector<std::string> configs;
    std::string abiMatch;
    bool strict = false;

    // EPM_BUILD_MISSING_BINARIES; unset means ON.
    std::optional<bool> buildMissingBinaries;
    std::map<std::string, PackageOverride> packages;

    MIRO_REFLECT(schema,
                 profile,
                 mode,
                 configs,
                 abiMatch,
                 strict,
                 buildMissingBinaries,
                 packages)
};

// An environment variable's value, or nullopt when unset; eacp::getEnv in the
// binary, a fixture in tests.
using EnvironmentLookup =
    std::function<std::optional<std::string>(std::string_view)>;

// "KEY=VALUE;KEY2=VALUE2" as a map; a bare KEY means ON.
std::map<std::string, std::string> parseOptionList(std::string_view text);

// `fromCMake` with every value it leaves unset taken from the environment:
// EPM_PROFILE, EPM_MODE, EPM_CONFIGS, EPM_ABI_MATCH, EPM_STRICT and
// EPM_BUILD_MISSING_BINARIES, then EPM_<Name>_MODE, _SOURCE, _CONFIGS and
// _OPTIONS for each of `names` (options merge by key). EPM_STRICT only turns
// strict on, since a CMake OFF cannot be told from an unset one.
Overrides withEnvironment(Overrides fromCMake,
                          const EA::Vector<std::string>& names,
                          const EnvironmentLookup& lookup);
} // namespace epm
