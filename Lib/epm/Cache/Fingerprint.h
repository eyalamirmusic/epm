#pragma once

#include <epm/Spec/Env.h>
#include <epm/Spec/Request.h>

#include <Miro/Reflect.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// What a binary cache entry was built for and from, recorded in its
// manifest.json. The entry's directory is the first 12 hex characters of the
// sha256 of the canonical form: Miro's compact output, `Miro::toJSONString`,
// whose objects are std::maps, so keys (options included) come out sorted.
struct AbiFingerprint
{
    int schema = 4;
    std::string platform;
    std::string compilerId;
    std::string compilerVersion;
    std::string compilerMajor;
    std::string compilerPath;
    std::string systemName;
    std::string systemProcessor;
    std::string osxArchitectures;
    std::string osxDeploymentTarget;
    std::string cxxStandard;

    // The Android API level and STL; empty off Android, where the ABI is in
    // the platform name.
    std::string androidApi;
    std::string androidStl;

    // The MSVC runtime library (msvcRuntimeOf); empty off the MSVC ABI.
    std::string msvcRuntime;
    std::string toolchainFile;

    // sha256 of the toolchain file's contents; empty without one.
    std::string toolchainFileHash;
    std::map<std::string, std::string> options;
    std::string sourceId;

    // Direct dependency name -> that dependency's fingerprint hash (the
    // request's deps). Every policy compares it.
    std::map<std::string, std::string> deps;

    MIRO_REFLECT(schema,
                 platform,
                 compilerId,
                 compilerVersion,
                 compilerMajor,
                 compilerPath,
                 systemName,
                 systemProcessor,
                 osxArchitectures,
                 osxDeploymentTarget,
                 cxxStandard,
                 androidApi,
                 androidStl,
                 msvcRuntime,
                 toolchainFile,
                 toolchainFileHash,
                 options,
                 sourceId,
                 deps)
};

// EPM_ABI_MATCH: how close an existing entry must be to be used.
//   strict      every recorded field equal
//   compatible  same platform, compiler id and major version, deployment
//               target, C++ standard, Android API level and STL, options,
//               source and toolchain file contents; compiler path,
//               minor/patch version and processor may differ
//   any         same platform, options and source
// Every policy also requires the same dependencies (deps) and, on the MSVC
// ABI, the same runtime library (msvcRuntime): nothing links across those.
enum class AbiMatch
{
    strict,
    compatible,
    any
};

std::optional<AbiMatch> parseAbiMatch(std::string_view text);

// "17" for "17.0.0.17000013"; the whole version when it has no dot.
std::string compilerMajor(std::string_view version);

// The fingerprint of building `request` with `env`. `toolchainFileHash` is
// left to the caller (makeFingerprint), so this stays free of file access.
AbiFingerprint fingerprintFor(const Request& request,
                              const Env& env,
                              const std::string& sourceId);

// fingerprintFor plus the sha256 of the toolchain file, when there is one.
AbiFingerprint makeFingerprint(const Request& request,
                               const Env& env,
                               const std::string& sourceId);

std::string canonicalFingerprint(const AbiFingerprint& fingerprint);

// sha256 of the canonical fingerprint, full hex; directories use the first 12.
std::string fingerprintId(const AbiFingerprint& fingerprint);

bool abiMatches(const AbiFingerprint& recorded,
                const AbiFingerprint& wanted,
                AbiMatch policy);

// How an entry built for `recorded` differs from the `current` toolchain, for
// the configure line ("built with AppleClang 17.0.0.17000013, current
// 17.0.0.17000319"); nullopt when the two are identical.
std::optional<std::string> abiNote(const AbiFingerprint& recorded,
                                   const AbiFingerprint& current);
} // namespace epm
