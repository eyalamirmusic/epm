#include "Prefix.h"

#include <epm/Build/Harvest.h>

namespace epm
{
Prefix::Prefix(BinaryEntry entryToUse,
               AbiFingerprint wantedToUse,
               eacp::Vector<std::string> configsToUse,
               eacp::FilePath stagedToUse)
    : binaryEntry(std::move(entryToUse))
    , wantedFingerprint(std::move(wantedToUse))
    , configsToBuild(std::move(configsToUse))
    , staged(std::move(stagedToUse))
{
}

const eacp::FilePath& Prefix::path() const
{
    return binaryEntry.prefix;
}

const eacp::Vector<std::string>& Prefix::configs() const
{
    return configsToBuild;
}

const eacp::FilePath& Prefix::stagedEntry() const
{
    return staged.empty() ? binaryEntry.prefix : staged;
}

eacp::FilePath Prefix::configDirectory(const std::string& config) const
{
    return epm::configDirectory(stagedEntry(), config);
}

const BinaryEntry& Prefix::entry() const
{
    return binaryEntry;
}

const AbiFingerprint& Prefix::wanted() const
{
    return wantedFingerprint;
}
} // namespace epm
