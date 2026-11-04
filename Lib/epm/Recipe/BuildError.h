#pragma once

#include <stdexcept>
#include <string>

namespace epm
{
// What the build step helpers (Package, CMakeBuild) throw when a step fails.
// The step's runner catches it and reports `what()` as the error line and
// `details` (the tail of a child build's output) above it; a build() may
// throw it too.
struct BuildError : std::runtime_error
{
    explicit BuildError(const std::string& error, std::string detailsToUse = {})
        : std::runtime_error(error)
        , details(std::move(detailsToUse))
    {
    }

    std::string details;
};
} // namespace epm
