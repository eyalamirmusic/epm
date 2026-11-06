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
// It holds no machine path, so another machine's entry for the same
// toolchain, SDK and source has the same key: the compiler and toolchain file
// count by identity (id, version, contents), never by where they are.
struct AbiFingerprint
{
    int schema = 4;
    std::string platform;
    std::string compilerId;
    std::string compilerVersion;
    std::string compilerMajor;

    // The C compiler's id and version, only when they are not the C++
    // compiler's (a project that enabled C with another compiler); empty for
    // the usual case, the same toolchain, and for a C driver epm.cmake found
    // beside the C++ compiler (Env::cCompiler).
    std::string cCompilerId;
    std::string cCompilerVersion;
    std::string systemName;
    std::string systemProcessor;
    std::string osxArchitectures;
    std::string osxDeploymentTarget;

    // Env's sdkVersion: the Apple or Windows SDK compiled against.
    std::string sdkVersion;
    std::string cxxStandard;

    // The MSVC runtime library (msvcRuntimeOf); empty off the MSVC ABI.
    std::string msvcRuntime;

    // sha256 of the toolchain file's contents; empty without one.
    std::string toolchainFileHash;

    // The flag variables a child build inherits from this process and CMake
    // (or cl) reads (inheritedFlags): name -> value, unset or empty ones left
    // out.
    std::map<std::string, std::string> envFlags;
    std::map<std::string, std::string> options;
    std::string sourceId;

    // Direct dependency name -> the fingerprint hash of the entry it was built
    // against (the request's deps), which strict compares.
    std::map<std::string, std::string> deps;

    // The same dependencies as compatible and any compare them: each entry's
    // abiKey under that policy (the request's compatibleDeps and anyDeps), so
    // an entry built elsewhere against an entry this machine would accept in
    // place of its own still matches (sameDependencies).
    std::map<std::string, std::string> compatibleDeps;
    std::map<std::string, std::string> anyDeps;

    MIRO_REFLECT(schema,
                 platform,
                 compilerId,
                 compilerVersion,
                 compilerMajor,
                 cCompilerId,
                 cCompilerVersion,
                 systemName,
                 systemProcessor,
                 osxArchitectures,
                 osxDeploymentTarget,
                 sdkVersion,
                 cxxStandard,
                 msvcRuntime,
                 toolchainFileHash,
                 envFlags,
                 options,
                 sourceId,
                 deps,
                 compatibleDeps,
                 anyDeps)
};

// EPM_ABI_MATCH: how close an existing entry must be to be used.
//   strict      every recorded field equal
//   compatible  same platform, compiler id and major version (the C
//               compiler's too, when recorded), deployment target, SDK major
//               version, C++ standard, toolchain file contents and inherited
//               flags; minor/patch versions of the compiler and the SDK and
//               the processor may differ
//   any         same platform, options and source
// Every policy also requires the same schema, options, source, dependencies
// (as the policy sees them: sameDependencies) and, on the MSVC ABI, runtime
// library (msvcRuntime): nothing links across those.
enum class AbiMatch
{
    strict,
    compatible,
    any
};

std::optional<AbiMatch> parseAbiMatch(std::string_view text);

// "17" for "17.0.0.17000013"; the whole version when it has no dot.
std::string majorVersion(std::string_view version);

// The environment variables a child build inherits that change what it
// compiles: CMake seeds CMAKE_<LANG>_FLAGS from CFLAGS, CXXFLAGS and the
// like and the linker flags from LDFLAGS on a build tree's first configure,
// and cl and clang-cl read CL and _CL_ on every call.
inline constexpr std::string_view inheritedFlagVariables[] = {"ASMFLAGS",
                                                              "CFLAGS",
                                                              "CL",
                                                              "CUDAFLAGS",
                                                              "CXXFLAGS",
                                                              "FFLAGS",
                                                              "HIPFLAGS",
                                                              "LDFLAGS",
                                                              "OBJCFLAGS",
                                                              "OBJCXXFLAGS",
                                                              "_CL_"};

// This process's inheritedFlagVariables that are set and not empty.
std::map<std::string, std::string> inheritedFlags();

// The fingerprint of building `request` with `env`. `toolchainFileHash` and
// `envFlags` are left to the caller (makeFingerprint), so this stays free of
// file and environment access.
AbiFingerprint fingerprintFor(const Request& request,
                              const Env& env,
                              const std::string& sourceId);

// fingerprintFor plus the sha256 of the toolchain file, when there is one,
// and this process's inheritedFlags, which the child build inherits.
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

// The fingerprint as `policy` sees it. strict keeps all of it. compatible
// drops what it ignores (the full compiler and C compiler versions, the
// processor, the SDK's minor version) and any keeps only the schema, platform,
// runtime library, options and source; both put the policy's dependency keys
// (compatibleDeps, anyDeps, else deps) in deps. Fingerprints the policy
// matches project to the same value. Dropping rather than keeping is
// deliberate for compatible: a field added later counts until it is dropped.
AbiFingerprint abiProjection(const AbiFingerprint& fingerprint, AbiMatch policy);

// fingerprintId of abiProjection: what a dependent records of the entry it
// was built against, one key per policy (Request::deps, compatibleDeps and
// anyDeps). The same on every machine whose entry that policy would accept.
std::string abiKey(const AbiFingerprint& fingerprint, AbiMatch policy);

// Whether the two were built against the same dependencies as `policy` sees
// them: strict compares deps; compatible and any compare each dependency's
// key for that policy (compatibleDeps, anyDeps), taking its deps hash where a
// fingerprint records no such key, so a request without keys is no looser.
bool sameDependencies(const AbiFingerprint& recorded,
                      const AbiFingerprint& wanted,
                      AbiMatch policy);
} // namespace epm
