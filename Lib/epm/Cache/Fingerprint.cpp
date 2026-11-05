#include "Fingerprint.h"

#include <epm/Spec/Platform.h>
#include <epm/Util/Sha256.h>
#include <epm/Util/Text.h>

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

std::string compilerMajor(std::string_view version)
{
    return std::string {version.substr(0, version.find('.'))};
}

AbiFingerprint fingerprintFor(const Request& request,
                              const Env& env,
                              const std::string& sourceId)
{
    auto fingerprint = AbiFingerprint {};
    fingerprint.platform = platformName(env);
    fingerprint.compilerId = env.cxxCompilerId;
    fingerprint.compilerVersion = env.cxxCompilerVersion;
    fingerprint.compilerMajor = compilerMajor(env.cxxCompilerVersion);
    fingerprint.compilerPath = env.cxxCompiler;
    fingerprint.systemName = env.systemName;
    fingerprint.systemProcessor = env.systemProcessor;
    fingerprint.osxArchitectures = env.osxArchitectures;
    fingerprint.osxDeploymentTarget = env.osxDeploymentTarget;
    fingerprint.cxxStandard = env.cxxStandard;
    fingerprint.msvcRuntime = msvcRuntimeOf(env);
    fingerprint.toolchainFile = env.toolchainFile;
    fingerprint.options = request.options;
    fingerprint.sourceId = sourceId;
    fingerprint.deps = request.deps;
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
        && recorded.deps == wanted.deps
        && recorded.msvcRuntime == wanted.msvcRuntime;

    if (policy == AbiMatch::any || !sameBase)
        return sameBase;

    return recorded.compilerId == wanted.compilerId
           && recorded.compilerMajor == wanted.compilerMajor
           && recorded.osxDeploymentTarget == wanted.osxDeploymentTarget
           && recorded.cxxStandard == wanted.cxxStandard
           && recorded.toolchainFileHash == wanted.toolchainFileHash;
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

    if (recorded.compilerPath != current.compilerPath)
        return "built with " + recorded.compilerPath + ", current "
               + current.compilerPath;

    auto fields = Vector<std::string> {};
    const auto compare = [&](const char* name, const auto& a, const auto& b)
    {
        if (a != b)
            fields.add(name);
    };

    compare("systemName", recorded.systemName, current.systemName);
    compare("systemProcessor", recorded.systemProcessor, current.systemProcessor);
    compare("osxArchitectures", recorded.osxArchitectures, current.osxArchitectures);
    compare("osxDeploymentTarget",
            recorded.osxDeploymentTarget,
            current.osxDeploymentTarget);
    compare("cxxStandard", recorded.cxxStandard, current.cxxStandard);
    compare("msvcRuntime", recorded.msvcRuntime, current.msvcRuntime);
    compare("toolchainFile", recorded.toolchainFile, current.toolchainFile);
    compare(
        "toolchainFileHash", recorded.toolchainFileHash, current.toolchainFileHash);
    compare("deps", recorded.deps, current.deps);

    return "built with a different " + joinList(fields, ", ");
}
} // namespace epm
