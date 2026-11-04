#include "Prefix.h"

namespace epm
{
Prefix::Prefix(BinaryEntry entryToUse,
               AbiFingerprint wantedToUse,
               eacp::Vector<std::string> configsToUse)
    : binaryEntry(std::move(entryToUse))
    , wantedFingerprint(std::move(wantedToUse))
    , configsToBuild(std::move(configsToUse))
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

const BinaryEntry& Prefix::entry() const
{
    return binaryEntry;
}

const AbiFingerprint& Prefix::wanted() const
{
    return wantedFingerprint;
}
} // namespace epm
