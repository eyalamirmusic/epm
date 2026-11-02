#pragma once

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <string_view>

namespace epm
{
// Target names from an export file's `add_library(<namespace>X ...)` lines,
// without the namespace.
eacp::Vector<std::string> exportedTargetNames(std::string_view exportFileText,
                                              std::string_view exportNamespace);

// True when the export file still points into the child build tree.
bool referencesBuildTree(std::string_view exportFileText,
                         const eacp::FilePath& buildDirectory);

// The epm-targets.cmake facade over export-Debug.cmake and export-Release.cmake.
std::string targetsFileText(const eacp::Vector<std::string>& targetNames);
} // namespace epm
