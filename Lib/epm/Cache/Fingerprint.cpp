#include "Fingerprint.h"

#include <epm/Spec/Platform.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Environment.h>
#include <eacp/Core/Utils/FilePath.h>

#include <Miro/Json.h>

namespace epm
{
using namespace eacp;

std::optional<AbiMatch> parseAbiMatch(std::string_view text)
{
    if (text == "strict")
        return AbiMatch::strict;

    if (text == "compatible")
        return AbiMatch::compatible;

    if (text == "any")
        return AbiMatch::any;

    return std::nullopt;
}

std::string majorVersion(std::string_view version)
{
    return std::string {version.substr(0, version.find('.'))};
}

std::map<std::string, std::string> inheritedFlags()
{
    auto flags = std::map<std::string, std::string> {};

    for (const auto name: inheritedFlagVariables)
        if (auto value = getEnvValue(name); !value.empty())
            flags.emplace(std::string {name}, std::move(value));

    return flags;
}

AbiFingerprint fingerprintFor(const Request& request,
                              const Env& env,
                              const std::string& sourceId)
{
    auto fingerprint = AbiFingerprint {};
    fingerprint.platform = platformName(env);
    fingerprint.compilerId = env.cxxCompilerId;
    fingerprint.compilerVersion = env.cxxCompilerVersion;
    fingerprint.compilerMajor = majorVersion(env.cxxCompilerVersion);

    if (!env.cCompilerId.empty()
        && (env.cCompilerId != env.cxxCompilerId
            || env.cCompilerVersion != env.cxxCompilerVersion))
    {
        fingerprint.cCompilerId = env.cCompilerId;
        fingerprint.cCompilerVersion = env.cCompilerVersion;
    }

    fingerprint.systemName = env.systemName;
    fingerprint.systemProcessor = env.systemProcessor;
    fingerprint.osxArchitectures = env.osxArchitectures;
    fingerprint.osxDeploymentTarget = env.osxDeploymentTarget;
    fingerprint.sdkVersion = env.sdkVersion;
    fingerprint.cxxStandard = env.cxxStandard;
    fingerprint.androidApi = env.androidApi;
    fingerprint.androidStl = env.androidStl;
    fingerprint.msvcRuntime = msvcRuntimeOf(env);
    fingerprint.options = request.options;
    fingerprint.sourceId = sourceId;
    fingerprint.deps = request.deps;
    fingerprint.compatibleDeps = request.compatibleDeps;
    fingerprint.anyDeps = request.anyDeps;
    return fingerprint;
}

AbiFingerprint makeFingerprint(const Request& request,
                               const Env& env,
                               const std::string& sourceId)
{
    auto fingerprint = fingerprintFor(request, env, sourceId);

    if (!env.toolchainFile.empty())
        fingerprint.toolchainFileHash =
            sha256HexOfFile(FilePath {env.toolchainFile}).value_or("unreadable");

    fingerprint.envFlags = inheritedFlags();
    return fingerprint;
}

std::string canonicalFingerprint(const AbiFingerprint& fingerprint)
{
    return Miro::toJSONString(fingerprint);
}

std::string fingerprintId(const AbiFingerprint& fingerprint)
{
    return sha256Hex(canonicalFingerprint(fingerprint));
}

bool abiMatches(const AbiFingerprint& recorded,
                const AbiFingerprint& wanted,
                AbiMatch policy)
{
    if (policy == AbiMatch::strict)
        return canonicalFingerprint(recorded) == canonicalFingerprint(wanted);

    const auto sameBase =
        recorded.schema == wanted.schema && recorded.platform == wanted.platform
        && recorded.options == wanted.options && recorded.sourceId == wanted.sourceId
        && sameDependencies(recorded, wanted, policy)
        && recorded.msvcRuntime == wanted.msvcRuntime;

    if (policy == AbiMatch::any || !sameBase)
        return sameBase;

    return recorded.compilerId == wanted.compilerId
           && recorded.compilerMajor == wanted.compilerMajor
           && recorded.cCompilerId == wanted.cCompilerId
           && majorVersion(recorded.cCompilerVersion)
                  == majorVersion(wanted.cCompilerVersion)
           && recorded.osxDeploymentTarget == wanted.osxDeploymentTarget
           && majorVersion(recorded.sdkVersion) == majorVersion(wanted.sdkVersion)
           && recorded.cxxStandard == wanted.cxxStandard
           && recorded.androidApi == wanted.androidApi
           && recorded.androidStl == wanted.androidStl
           && recorded.toolchainFileHash == wanted.toolchainFileHash
           && recorded.envFlags == wanted.envFlags;
}

std::optional<std::string> abiNote(const AbiFingerprint& recorded,
                                   const AbiFingerprint& current)
{
    if (canonicalFingerprint(recorded) == canonicalFingerprint(current))
        return std::nullopt;

    if (recorded.compilerId != current.compilerId)
        return "built with " + recorded.compilerId + " " + recorded.compilerVersion
               + ", current " + current.compilerId + " " + current.compilerVersion;

    if (recorded.compilerVersion != current.compilerVersion)
        return "built with " + recorded.compilerId + " " + recorded.compilerVersion
               + ", current " + current.compilerVersion;

    auto fields = Vector<std::string> {};
    const auto compare = [&](const char* name, const auto& a, const auto& b)
    {
        if (a != b)
            fields.add(name);
    };

    compare("cCompilerId", recorded.cCompilerId, current.cCompilerId);
    compare("cCompilerVersion", recorded.cCompilerVersion, current.cCompilerVersion);
    compare("systemName", recorded.systemName, current.systemName);
    compare("systemProcessor", recorded.systemProcessor, current.systemProcessor);
    compare("osxArchitectures", recorded.osxArchitectures, current.osxArchitectures);
    compare("osxDeploymentTarget",
            recorded.osxDeploymentTarget,
            current.osxDeploymentTarget);
    compare("sdkVersion", recorded.sdkVersion, current.sdkVersion);
    compare("cxxStandard", recorded.cxxStandard, current.cxxStandard);
    compare("androidApi", recorded.androidApi, current.androidApi);
    compare("androidStl", recorded.androidStl, current.androidStl);
    compare("msvcRuntime", recorded.msvcRuntime, current.msvcRuntime);
    compare(
        "toolchainFileHash", recorded.toolchainFileHash, current.toolchainFileHash);
    compare("envFlags", recorded.envFlags, current.envFlags);
    compare("deps", recorded.deps, current.deps);

    return "built with a different " + joinList(fields, ", ");
}

namespace
{
// Each dependency's key for `policy`: its deps hash, replaced by the
// policy's own key where the fingerprint records one.
std::map<std::string, std::string> dependencyKeys(const AbiFingerprint& fingerprint,
                                                  AbiMatch policy)
{
    auto keys = fingerprint.deps;

    if (policy == AbiMatch::strict)
        return keys;

    const auto& policyKeys = policy == AbiMatch::compatible
                                 ? fingerprint.compatibleDeps
                                 : fingerprint.anyDeps;

    for (const auto& [name, key]: policyKeys)
        keys[name] = key;

    return keys;
}
} // namespace

AbiFingerprint abiProjection(const AbiFingerprint& fingerprint, AbiMatch policy)
{
    if (policy == AbiMatch::strict)
        return fingerprint;

    auto projected = AbiFingerprint {};

    if (policy == AbiMatch::compatible)
    {
        projected = fingerprint;
        projected.compilerVersion.clear();
        projected.cCompilerVersion = majorVersion(fingerprint.cCompilerVersion);
        projected.systemName.clear();
        projected.systemProcessor.clear();
        projected.osxArchitectures.clear();
        projected.sdkVersion = majorVersion(fingerprint.sdkVersion);
        projected.compatibleDeps.clear();
        projected.anyDeps.clear();
    }
    else
    {
        projected.schema = fingerprint.schema;
        projected.platform = fingerprint.platform;
        projected.msvcRuntime = fingerprint.msvcRuntime;
        projected.options = fingerprint.options;
        projected.sourceId = fingerprint.sourceId;
    }

    projected.deps = dependencyKeys(fingerprint, policy);
    return projected;
}

std::string abiKey(const AbiFingerprint& fingerprint, AbiMatch policy)
{
    return fingerprintId(abiProjection(fingerprint, policy));
}

bool sameDependencies(const AbiFingerprint& recorded,
                      const AbiFingerprint& wanted,
                      AbiMatch policy)
{
    return dependencyKeys(recorded, policy) == dependencyKeys(wanted, policy);
}
} // namespace epm
