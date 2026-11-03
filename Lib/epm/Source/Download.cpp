#include <epm/Source/Download.h>
#include <epm/Lock/Messages.h>
#include <eacp/Core/Utils/Sha256.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Network/HTTP/Http.h>

#include <stdexcept>

namespace epm
{
namespace
{
constexpr auto httpOkStatus = 200;
} // namespace

void downloadFile(const std::string& url,
                  const eacp::FilePath& destination,
                  const ExpectedHash& expected)
{
    auto partial = destination.withSuffix(".part");
    eacp::Files::createDirectories(destination.parentDirectory());

    auto response =
        eacp::HTTP::downloadFile(eacp::HTTP::Request {url}, partial.str());

    if (!response.error.empty() || response.statusCode != httpOkStatus)
    {
        eacp::Files::removeFile(partial);
        throw std::runtime_error("downloading " + url + " failed with HTTP status "
                                 + std::to_string(response.statusCode) + ": "
                                 + response.error);
    }

    if (!expected.hex.empty())
    {
        auto actual = expected.computeHex(partial);

        if (actual != expected.hex)
        {
            eacp::Files::removeFile(partial);
            throw HashMismatchError(formatUserError(
                url + " hashes to " + expected.algorithm + "=" + actual
                    + ", not the expected " + expected.algorithm + "=" + expected.hex
                    + "; the download was deleted",
                "check that the hash pinned for it is the one its publisher gives"));
        }
    }

    if (!eacp::Files::moveFile(partial, destination))
        throw std::runtime_error("cannot move the download into "
                                 + destination.str());
}
} // namespace epm
