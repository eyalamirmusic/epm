#pragma once

#include <eacp/Core/Utils/FilePath.h>

namespace epm
{
// The local cache. Each accessor creates its directory on first use.
class CacheRoot
{
public:
    explicit CacheRoot(eacp::FilePath rootToUse);

    // $EPM_CACHE, else <platform cache dir>/epm.
    static CacheRoot fromEnvironment();

    const eacp::FilePath& path() const;

    eacp::FilePath src() const;
    eacp::FilePath git() const;
    eacp::FilePath downloads() const;
    eacp::FilePath bin() const;
    eacp::FilePath build() const;
    eacp::FilePath cpm() const;
    eacp::FilePath tmp() const;
    eacp::FilePath locks() const;

private:
    eacp::FilePath subdirectory(std::string_view name) const;

    eacp::FilePath root;
};
} // namespace epm
