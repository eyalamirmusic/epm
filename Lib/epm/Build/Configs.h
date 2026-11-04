#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// The configuration a requested name is built as: Debug and Release as they
// are, RelWithDebInfo and MinSizeRel as Release (any case); nullopt for any
// other name.
std::optional<std::string> buildConfigFor(std::string_view name);

// The names in canonical order (Debug, Release, RelWithDebInfo, MinSizeRel,
// then any others as given), without duplicates.
EA::Vector<std::string> canonicalConfigs(const EA::Vector<std::string>& names);

// What a request's configs are built as: mapped by buildConfigFor, canonical.
// Names buildConfigFor rejects are left out (requestError reports them).
EA::Vector<std::string> buildConfigsFor(const EA::Vector<std::string>& names);

// The configs in `wanted` that `present` lacks, canonical.
EA::Vector<std::string> missingConfigs(const EA::Vector<std::string>& present,
                                       const EA::Vector<std::string>& wanted);

// Which available config a consumer config links: the same name (any case),
// else Release, else the first available. `available` must not be empty.
std::string mapConsumerConfig(std::string_view consumer,
                              const EA::Vector<std::string>& available);

// The export namespace of a config's child build: epm_debug::, epm_release::.
std::string exportNamespace(std::string_view config);

// The generator expression a facade target links, choosing the per-config
// imported target by $<CONFIG> as mapConsumerConfig maps it, e.g. for
// {Debug, Release}: $<IF:$<CONFIG:Debug>,epm_debug::T,epm_release::T>.
std::string configSelector(const EA::Vector<std::string>& available,
                           std::string_view target);
} // namespace epm
