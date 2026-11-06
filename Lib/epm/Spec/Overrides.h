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
// EPM_<Name>_MODE, _SOURCE, _CONFIGS, _OPTIONS and _FETCH_SOURCE.
struct PackageOverride
{
    std::string mode;

    // A local checkout used instead of fetching; makes the package source.
    std::string source;
    eacp::Vector<std::string> configs;
    std::map<std::string, std::string> options;

    // EPM_<Name>_FETCH_SOURCE: Overrides::fetchSources for this package
    // alone; unset follows it.
    std::optional<bool> fetchSource;

    MIRO_REFLECT(mode, source, configs, options, fetchSource)
};

// overrides.json (Docs/Graph.md §3), written by get_epm.cmake from the CMake
// variables it sees. Unset strings and lists are empty.
struct Overrides
{
    int schema = 1;
    std::string mode;

    // ["auto"] asks for the PoC's auto rule explicitly.
    eacp::Vector<std::string> configs;
    std::string abiMatch;
    bool strict = false;

    // EPM_BUILD_MISSING_BINARIES; unset means ON.
    std::optional<bool> buildMissingBinaries;

    // EPM_FETCH_SOURCES; unset means ON: the source snapshot of every
    // prebuilt package is fetched, for its <name>_SOURCE_DIR (Docs/Graph.md
    // §7). OFF fetches only what a package built from source needs.
    std::optional<bool> fetchSources;
    std::map<std::string, PackageOverride> packages;

    MIRO_REFLECT(schema,
                 mode,
                 configs,
                 abiMatch,
                 strict,
                 buildMissingBinaries,
                 fetchSources,
                 packages)
};

// Whether the consumer wants the source of the prebuilt `name` fetched for
// its SOURCE_DIR: EPM_<Name>_FETCH_SOURCE, else EPM_FETCH_SOURCES, else yes.
bool fetchesSourceOf(const Overrides& overrides, const std::string& name);

// An environment variable's value, or nullopt when unset; eacp::getEnv in the
// binary, a fixture in tests.
using EnvironmentLookup =
    std::function<std::optional<std::string>(std::string_view)>;

// "KEY=VALUE;KEY2=VALUE2" as a map; a bare KEY means ON.
std::map<std::string, std::string> parseOptionList(std::string_view text);

// `fromCMake` with every value it leaves unset taken from the environment:
// EPM_MODE, EPM_CONFIGS, EPM_ABI_MATCH, EPM_STRICT, EPM_BUILD_MISSING_BINARIES
// and EPM_FETCH_SOURCES, then EPM_<Name>_MODE, _SOURCE, _CONFIGS, _OPTIONS
// and _FETCH_SOURCE for each of `names` (options merge by key). EPM_STRICT only turns
// strict on, since a CMake OFF cannot be told from an unset one.
Overrides withEnvironment(Overrides fromCMake,
                          const eacp::Vector<std::string>& names,
                          const EnvironmentLookup& lookup);
} // namespace epm
