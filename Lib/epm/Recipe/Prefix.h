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
           eacp::Vector<std::string> configsToUse,
           eacp::FilePath stagedToUse = {});

    // bin/<name>/<platform>/<fp12>.
    const eacp::FilePath& path() const;

    // Inside the callback of cached(): the configs to build, which the entry
    // lacks. Once cached() returns: the configs that call built (empty when
    // the entry already held every one).
    const eacp::Vector<std::string>& configs() const;

    // Inside the callback of cached(): the private directory, laid out as the
    // entry, that the configs are built into (configs/<Config>/, gen/shared/),
    // which cached() publishes into the entry once the callback has returned.
    // Otherwise path().
    const eacp::FilePath& stagedEntry() const;

    // Where `config`'s files go, configDirectory(stagedEntry(), config): the
    // unit a step's build and an afterBuild hook add its files to, inside the
    // callback, and the published one afterwards, which is never modified.
    eacp::FilePath configDirectory(const std::string& config) const;

    // The entry as cached() picked or created it; the configs it holds are
    // those published when the Prefix was made.
    const BinaryEntry& entry() const;

    // The fingerprint the step asked for, which may differ from the entry's
    // under a compatible or any abiMatch.
    const AbiFingerprint& wanted() const;

private:
    BinaryEntry binaryEntry;
    AbiFingerprint wantedFingerprint;
    eacp::Vector<std::string> configsToBuild;
    eacp::FilePath staged;
};
} // namespace epm
