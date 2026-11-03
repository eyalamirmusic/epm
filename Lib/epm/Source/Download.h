#pragma once

#include <eacp/Core/Utils/Sha256.h>

#include <eacp/Core/Utils/FilePath.h>

#include <functional>
#include <stdexcept>
#include <string>

namespace epm
{
class HashMismatchError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

// The hash a download must have; an empty `hex` checks nothing.
struct ExpectedHash
{
    std::string algorithm = "SHA256";
    std::string hex;
    std::function<std::string(const eacp::FilePath&)> computeHex =
        [](const eacp::FilePath& file) { return eacp::computeFileSha256Hex(file); };
};

// Downloads `url` to a .part sibling of `destination` and moves it into place
// only once it is whole and hashes to `expected`; a file that does not is
// deleted and HashMismatchError thrown.
void downloadFile(const std::string& url,
                  const eacp::FilePath& destination,
                  const ExpectedHash& expected);
} // namespace epm
