#pragma once

#include <epm/Graph/ResolvedGraph.h>

#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>

namespace epm
{
// graph.cmake: epm_PACKAGES in dependency order, then per
// package MODE, SOURCE_DIR, DEPS and OPTIONS (KEY=VALUE), and for a prebuilt
// one PREFIX, TARGETS_FILE, PLATFORM, CONFIGS and NOTE; last epm_NOTES.
// Semicolons inside an option or a note are escaped, so each stays one list
// element.
std::string graphCMakeText(const ResolvedGraph& graph);

} // namespace epm
