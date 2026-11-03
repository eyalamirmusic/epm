#pragma once

#include "CacheRoot.h"

#include <cstdint>
#include <string>

namespace epm
{
// "412.3 MB", "0 B".
std::string humanSize(std::uint64_t bytes);

// The bytes of every regular file under `directory`.
std::uint64_t directorySize(const eacp::FilePath& directory);

// `epm cache ls`: one line per binary entry (name, platform, configs,
// compiler and major version, size, fp12), then the sources and the tools.
std::string cacheListing(const CacheRoot& cache);
} // namespace epm
