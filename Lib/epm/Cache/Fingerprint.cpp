#include "Fingerprint.h"

#include <epm/Spec/Platform.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/FilePath.h>
#include <eacp/Core/Utils/Strings.h>
#include <eacp/Core/Utils/Sha256.h>

#include <Miro/Json.h>

namespace epm
{
using namespace eacp;

namespace
{
// "unreadable" stands for a toolchain file that cannot be read, so it still
// changes the fingerprint.
std::string toolchainFileHash(const FilePath& file)
{
    auto hash = computeFileSha256Hex(file);

    if (hash.empty())
        return "unreadable";

    return hash;
}
} // namespace

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
                              const std::string& sourceHash)
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
    fingerprint.toolchainFile = env.toolchainFile;
    fingerprint.options = request.options;
    fingerprint.sourceHash = sourceHash;
    fingerprint.deps = request.deps;
    return fingerprint;
}

AbiFingerprint makeFingerprint(const Request& request,
                               const Env& env,
                               const std::string& sourceHash)
{
    auto fingerprint = fingerprintFor(request, env, sourceHash);

    if (!env.toolchainFile.empty())
        fingerprint.toolchainFileHash =
            toolchainFileHash(FilePath {env.toolchainFile});

    return fingerprint;
}

std::string canonicalFingerprint(const AbiFingerprint& fingerprint)
{
    return Miro::toJSONString(fingerprint);
}

std::string fingerprintId(const AbiFingerprint& fingerprint)
{
    return eacp::computeSha256Hex(canonicalFingerprint(fingerprint));
}

bool abiMatches(const AbiFingerprint& recorded,
                const AbiFingerprint& wanted,
                AbiMatch policy)
{
    if (policy == AbiMatch::strict)
        return canonicalFingerprint(recorded) == canonicalFingerprint(wanted);

    const auto sameBase =
        recorded.schema == wanted.schema && recorded.platform == wanted.platform
        && recorded.options == wanted.options
        && recorded.sourceHash == wanted.sourceHash && recorded.deps == wanted.deps;

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

    auto fields = EA::Vector<std::string> {};
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
    compare("toolchainFile", recorded.toolchainFile, current.toolchainFile);
    compare(
        "toolchainFileHash", recorded.toolchainFileHash, current.toolchainFileHash);
    compare("deps", recorded.deps, current.deps);

    return "built with a different " + eacp::Strings::join(fields, ", ");
}
} // namespace epm
