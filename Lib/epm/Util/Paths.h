#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <string>

namespace epm
{
inline eacp::FilePath getSiblingWithSuffix(const eacp::FilePath& path,
                                           const std::string& suffix)
{
    return path.sibling(path.fileName() + suffix);
}
} // namespace epm
