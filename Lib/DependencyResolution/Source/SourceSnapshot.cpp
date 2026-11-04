#include <DependencyResolution/Source/SourceSnapshot.h>

namespace DependencyResolution
{
using namespace eacp;

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

} // namespace DependencyResolution
