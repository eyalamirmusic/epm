#pragma once

#include "BinaryCache.h"
#include "CacheRoot.h"

#include <string>

namespace epm
{
// The problems of one published config of the entry at `entry`: its
// epm-files.json missing or not the one the marker recorded, else each file
// that lists (unit files relative to its unit, shared ones relative to the
// entry) that is missing or whose size or sha256 differs, and each file of
// its unit it does not list. Empty when the config is intact.
eacp::Vector<std::string> verifyConfig(const eacp::FilePath& entry,
                                       const ConfigMarker& marker);

struct CacheVerification
{
    int entries = 0;
    int configs = 0;

    // Entries of an older layout, which epm ignores: not problems.
    int ignored = 0;

    // Staging directories: builds in progress, or what a crash left.
    int staging = 0;

    // One line each, "<name>/<platform>/<fp12>[ <Config>]: <problem>".
    eacp::Vector<std::string> problems;
};

// `epm cache verify`: re-hashes every published config of every entry
// (verifyConfig) and reports an entry directory without a manifest, a facade
// that is not targetsFileText(), and a unit without a valid marker.
CacheVerification verifyCache(const CacheRoot& cache);

// The problems, one per line, then a summary line.
std::string verificationText(const CacheVerification& verification);
} // namespace epm
