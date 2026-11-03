#include "SourceCache.h"

#include <epm/Util/Sha256.h>

namespace epm
{
using namespace eacp;

std::string sourceId(std::string_view url, std::string_view commit)
{
    return computeSha256Hex("git\n" + std::string {url} + "\n"
                            + std::string {commit});
}

bool isFullCommitHash(std::string_view ref)
{
    if (ref.size() != 40)
        return false;

    for (const auto c: ref)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')
              || (c >= 'A' && c <= 'F')))
            return false;

    return true;
}

std::optional<std::string> resolveRef(const Vector<Git::RemoteRef>& refs,
                                      std::string_view ref)
{
    if (isFullCommitHash(ref))
        return std::string {ref};

    const auto name = std::string {ref};
    const auto candidates = Vector<std::string> {"refs/tags/" + name + "^{}",
                                                 "refs/tags/" + name,
                                                 "refs/heads/" + name,
                                                 name};

    for (const auto& candidate: candidates)
        for (const auto& remote: refs)
            if (remote.ref == candidate)
                return remote.sha;

    return std::nullopt;
}

} // namespace epm
