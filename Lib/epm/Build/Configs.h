#pragma once

#include <epm/Spec/Env.h>

#include <eacp/Core/Utils/Containers.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// The configuration a requested name is built as: Debug, Release and
// RelWithDebInfo as they are, MinSizeRel as Release (any case); nullopt for
// any other name.
std::optional<std::string> buildConfigFor(std::string_view name);

// The name is built as Debug: against a debug runtime on the MSVC ABI.
bool isDebugConfig(std::string_view name);

// The names in canonical order (Debug, Release, RelWithDebInfo, MinSizeRel,
// then any others as given), without duplicates.
eacp::Vector<std::string> canonicalConfigs(const eacp::Vector<std::string>& names);

// What a request's configs are built as: mapped by buildConfigFor, canonical.
// Names buildConfigFor rejects are left out (requestError reports them).
eacp::Vector<std::string> buildConfigsFor(const eacp::Vector<std::string>& names);

// The configs in `wanted` that `present` lacks, canonical.
eacp::Vector<std::string> missingConfigs(const eacp::Vector<std::string>& present,
                                         const eacp::Vector<std::string>& wanted);

// Which available config a consumer config links: the same name (any case),
// else Release, else RelWithDebInfo, else the first available. `available`
// must not be empty.
std::string mapConsumerConfig(std::string_view consumer,
                              const eacp::Vector<std::string>& available);

// Why the consumer cannot link a package whose configs are `available`, on
// the MSVC ABI: a build of the consumer (its build type, or each type of a
// multi-config generator) maps to a config of the other kind, Debug against
// release or the reverse, and the debug and release runtimes do not link
// (_ITERATOR_DEBUG_LEVEL, RuntimeLibrary). nullopt elsewhere, when every
// build maps to its own kind, or when the consumer's runtime has no debug
// variant (msvcRuntimeHasDebug), which links release archives from Debug.
std::optional<std::string>
    configLinkProblem(const Env& env, const eacp::Vector<std::string>& available);

// The export namespace of a config's child build: epm_debug::, epm_release::.
std::string exportNamespace(std::string_view config);

// The generator expression a facade target links, choosing the per-config
// imported target by $<CONFIG> as mapConsumerConfig maps it, e.g. for
// {Debug, Release}: $<IF:$<CONFIG:Debug>,epm_debug::T,epm_release::T>.
std::string configSelector(const eacp::Vector<std::string>& available,
                           std::string_view target);
} // namespace epm
