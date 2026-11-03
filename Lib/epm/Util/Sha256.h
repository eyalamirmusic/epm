#pragma once

#include <eacp/Core/Utils/FilePath.h>

#include <array>
#include <filesystem>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace epm
{
constexpr auto sha256DigestSize = 32;
constexpr auto sha256BlockSize = 64;
constexpr auto sha256WordCount = 8;

using Sha256Digest = std::array<std::uint8_t, sha256DigestSize>;

class Sha256
{
public:
    Sha256();

    void update(std::span<const std::uint8_t> bytes);
    void update(std::string_view text);
    Sha256Digest finish();

private:
    void processBlock(std::span<const std::uint8_t, sha256BlockSize> block);

    std::array<std::uint32_t, sha256WordCount> state {};
    std::array<std::uint8_t, sha256BlockSize> pending {};
    std::size_t pendingSize = 0;
    std::uint64_t totalBytes = 0;
};

std::string convertDigestToHex(const Sha256Digest& digest);
std::string computeSha256Hex(std::string_view text);
Sha256Digest computeFileSha256(const eacp::FilePath& file);
Sha256Digest computeFileSha256(const std::filesystem::path& file);
std::string computeFileSha256Hex(const eacp::FilePath& file);
std::string computeFileSha256Hex(const std::filesystem::path& file);
} // namespace epm
