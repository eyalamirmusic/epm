#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string_view>

namespace epm
{
// The local cache, and the read-only cache stores it falls back to for binary
// entries it lacks (CacheStore.h). Each accessor creates its directory on
// first use; nothing ever creates or writes anything in a store.
class CacheRoot
{
public:
    explicit CacheRoot(eacp::FilePath rootToUse,
                       eacp::Vector<eacp::FilePath> storesToUse = {});

    // $EPM_CACHE, else <platform cache dir>/epm, with storesFromEnvironment().
    static CacheRoot fromEnvironment();

    // $EPM_CACHE_STORES, split at the platform's path-list separator (':',
    // ';' on Windows), empty items dropped.
    static eacp::Vector<eacp::FilePath> storesFromEnvironment();

    // `text` split at the path-list separator, empty items dropped.
    static eacp::Vector<eacp::FilePath> storeList(std::string_view text);

    const eacp::FilePath& path() const;

    // The cache stores, in the order they are searched.
    const eacp::Vector<eacp::FilePath>& stores() const;

    eacp::FilePath src() const;
    eacp::FilePath bin() const;
    eacp::FilePath build() const;
    eacp::FilePath cpm() const;
    eacp::FilePath tmp() const;
    eacp::FilePath locks() const;

    // Compiled epm.cpp modules, by RecipeBuild's key.
    eacp::FilePath recipe() const;

private:
    eacp::FilePath subdirectory(std::string_view name) const;

    eacp::FilePath root;
    eacp::Vector<eacp::FilePath> storeRoots;
};
} // namespace epm
