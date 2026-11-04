#pragma once

#include <epm/Cache/BinaryCache.h>

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace epm
{
// A binary cache entry that Package::cached() hands a build step: where to
// build into, and which configs to build. Copies are cheap.
class Prefix
{
public:
    Prefix() = default;
    Prefix(BinaryEntry entryToUse,
           AbiFingerprint wantedToUse,
           eacp::Vector<std::string> configsToUse);

    // bin/<name>/<platform>/<fp12>.
    const eacp::FilePath& path() const;

    // Inside the callback of cached(): the configs to build, which the entry
    // lacks. Once cached() returns: the configs that call built (empty when
    // the entry already held every one).
    const eacp::Vector<std::string>& configs() const;

    // The entry as cached() picked or created it; the configs it holds are
    // those marked when the Prefix was made.
    const BinaryEntry& entry() const;

    // The fingerprint the step asked for, which may differ from the entry's
    // under a compatible or any abiMatch.
    const AbiFingerprint& wanted() const;

private:
    BinaryEntry binaryEntry;
    AbiFingerprint wantedFingerprint;
    eacp::Vector<std::string> configsToBuild;
};
} // namespace epm
