#include "Lto.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <optional>

namespace epm
{
namespace
{
constexpr auto archiveMagic = std::string_view {"!<arch>\n"};
constexpr auto archiveHeaderSize = std::size_t {60};
constexpr auto bsdLongName = std::string_view {"#1/"};

constexpr auto bitcodeMagic = std::string_view {"BC\xC0\xDE", 4};
constexpr auto bitcodeWrapperMagic = std::string_view {"\xDE\xC0\x17\x0B", 4};
constexpr auto elfMagic = std::string_view {"\x7F"
                                            "ELF",
                                            4};
constexpr auto gccLtoSection = std::string_view {".gnu.lto_"};
constexpr auto gccLtoMachOSegment = std::string_view {"__GNU_LTO"};

// An anonymous COFF object (IMAGE_FILE_MACHINE_UNKNOWN, then 0xFFFF) whose
// class ID, at offset 12, is the one cl.exe gives a /GL object: the same
// bytes LLVM's COFF::ClGlObjMagic holds.
constexpr auto anonymousObjectMagic = std::string_view {"\0\0\xFF\xFF", 4};
constexpr auto anonymousObjectClassIdAt = std::size_t {12};
constexpr auto msvcLtcgClassId =
    std::string_view {"\x38\xFE\xB3\x0C\xA5\xD9\xAB\x4D"
                      "\xAC\x9B\xD6\xB6\x22\x26\x53\xC2",
                      16};

constexpr auto fatMagic = std::uint32_t {0xCAFEBABE};
constexpr auto fat64Magic = std::uint32_t {0xCAFEBABF};

// More slices than any universal file has: a Java class file shares the
// magic, and its next field is a version far above this.
constexpr auto maxFatSlices = std::uint64_t {32};

// What a container may hold: a universal file's slices are archives or
// objects, an archive's members objects only, so no input recurses deeper
// than that, whatever its offsets say.
enum class Nesting
{
    anything,
    archiveOrObject,
    objectOnly
};

std::uint64_t bigEndian(std::string_view bytes, std::size_t at, std::size_t width)
{
    auto value = std::uint64_t {0};

    for (auto i = std::size_t {0}; i < width; ++i)
        value = (value << 8) | static_cast<unsigned char>(bytes[at + i]);

    return value;
}

std::uint64_t littleEndian(std::string_view bytes, std::size_t at, std::size_t width)
{
    auto value = std::uint64_t {0};

    for (auto i = width; i > 0; --i)
        value = (value << 8) | static_cast<unsigned char>(bytes[at + i - 1]);

    return value;
}

// A COFF object as MinGW's GCC writes it: x86, x64 or arm64.
bool isCoffObject(std::string_view bytes)
{
    if (bytes.size() < 20)
        return false;

    const auto machine = littleEndian(bytes, 0, 2);
    return machine == 0x14c || machine == 0x8664 || machine == 0xaa64;
}

bool isMsvcLtcgObject(std::string_view bytes)
{
    return bytes.starts_with(anonymousObjectMagic)
           && bytes.size() >= anonymousObjectClassIdAt + msvcLtcgClassId.size()
           && bytes.substr(anonymousObjectClassIdAt, msvcLtcgClassId.size())
                  == msvcLtcgClassId;
}

// A 16-byte, NUL-padded Mach-O name.
bool machONameIs(std::string_view bytes, std::size_t at, std::string_view name)
{
    const auto field = bytes.substr(at, 16);
    return field.substr(0, field.find('\0')) == name;
}

// Whether a Mach-O object (32 or 64 bit, either byte order) has a segment
// or a section in the segment GCC puts its LTO IR in, __GNU_LTO. Every read
// is checked against the object's size and its load commands' own sizes.
bool hasGccLtoMachOSegment(std::string_view bytes)
{
    if (bytes.size() < 4)
        return false;

    const auto magic = bigEndian(bytes, 0, 4);
    const auto wide = magic == 0xFEEDFACF || magic == 0xCFFAEDFE;
    const auto swapped = magic == 0xCEFAEDFE || magic == 0xCFFAEDFE;

    if (!wide && magic != 0xFEEDFACE && magic != 0xCEFAEDFE)
        return false;

    const auto read = [&](std::size_t at)
    { return swapped ? littleEndian(bytes, at, 4) : bigEndian(bytes, at, 4); };

    const auto headerSize = std::size_t {wide ? 32u : 28u};

    if (bytes.size() < headerSize)
        return false;

    const auto commandCount = read(16);
    const auto segmentCommand = std::uint64_t {wide ? 0x19u : 0x1u};
    const auto sectionsAt = std::size_t {wide ? 72u : 56u};
    const auto sectionCountAt = std::size_t {wide ? 64u : 48u};
    const auto sectionSize = std::size_t {wide ? 80u : 68u};
    auto offset = headerSize;

    for (auto i = std::uint64_t {0}; i < commandCount; ++i)
    {
        if (offset + 8 > bytes.size())
            return false;

        const auto command = read(offset);
        const auto commandSize = read(offset + 4);

        if (commandSize < 8 || commandSize > bytes.size() - offset)
            return false;

        if (command == segmentCommand && commandSize >= sectionsAt)
        {
            if (machONameIs(bytes, offset + 8, gccLtoMachOSegment))
                return true;

            const auto sections = read(offset + sectionCountAt);

            for (auto s = std::uint64_t {0}; s < sections; ++s)
            {
                const auto section = sectionsAt + s * sectionSize;

                if (section + sectionSize > commandSize)
                    break;

                // section(_64): sectname[16], then segname[16].
                if (machONameIs(bytes, offset + section + 16, gccLtoMachOSegment))
                    return true;
            }
        }

        offset += commandSize;
    }

    return false;
}

std::optional<std::uint64_t> decimalField(std::string_view field)
{
    const auto begin = field.find_first_not_of(' ');

    if (begin == std::string_view::npos)
        return std::nullopt;

    auto value = std::uint64_t {0};
    const auto* start = field.data() + begin;
    const auto [end, error] =
        std::from_chars(start, field.data() + field.size(), value);

    if (error != std::errc {} || end == start)
        return std::nullopt;

    return value;
}

std::string ltoKindOfObject(std::string_view bytes)
{
    if (bytes.starts_with(bitcodeMagic) || bytes.starts_with(bitcodeWrapperMagic))
        return std::string {llvmBitcode};

    if (isMsvcLtcgObject(bytes))
        return std::string {msvcLtcg};

    if ((bytes.starts_with(elfMagic) || isCoffObject(bytes))
        && bytes.find(gccLtoSection) != std::string_view::npos)
        return std::string {gccLto};

    if (hasGccLtoMachOSegment(bytes))
        return std::string {gccLto};

    return {};
}

std::string ltoKindIn(std::string_view bytes, Nesting nesting);

std::string ltoKindOfArchive(std::string_view bytes)
{
    auto offset = archiveMagic.size();

    while (offset + archiveHeaderSize <= bytes.size())
    {
        const auto header = bytes.substr(offset, archiveHeaderSize);
        const auto size = decimalField(header.substr(48, 10));

        if (header.substr(58, 2) != "`\n" || !size)
            break;

        offset += archiveHeaderSize;
        auto member = bytes.substr(
            offset, std::min<std::uint64_t>(*size, bytes.size() - offset));

        // A BSD long name is stored at the start of the member's data.
        if (const auto name = header.substr(0, 16); name.starts_with(bsdLongName))
            member.remove_prefix(std::min<std::uint64_t>(
                decimalField(name.substr(bsdLongName.size())).value_or(0),
                member.size()));

        if (auto kind = ltoKindIn(member, Nesting::objectOnly); !kind.empty())
            return kind;

        if (*size > bytes.size() - offset)
            break;

        offset += *size + *size % 2;
    }

    return {};
}

std::string ltoKindOfUniversal(std::string_view bytes, bool wide)
{
    const auto count = bigEndian(bytes, 4, 4);
    const auto entrySize = std::size_t {wide ? 32u : 20u};

    if (count > maxFatSlices || 8 + count * entrySize > bytes.size())
        return {};

    // A slice starts after the header and its table, and ends inside the
    // file: one that overlaps them (offset 0, say) is the file again.
    const auto firstSliceAt = 8 + count * entrySize;

    for (auto i = std::uint64_t {0}; i < count; ++i)
    {
        const auto entry = 8 + i * entrySize;
        const auto width = std::size_t {wide ? 8u : 4u};
        const auto offset = bigEndian(bytes, entry + 8, width);
        const auto size = bigEndian(bytes, entry + 8 + width, width);

        if (offset < firstSliceAt || offset > bytes.size()
            || size > bytes.size() - offset)
            continue;

        if (auto kind =
                ltoKindIn(bytes.substr(offset, size), Nesting::archiveOrObject);
            !kind.empty())
            return kind;
    }

    return {};
}

std::string ltoKindIn(std::string_view bytes, Nesting nesting)
{
    if (bytes.starts_with(archiveMagic))
        return nesting == Nesting::objectOnly ? std::string {}
                                              : ltoKindOfArchive(bytes);

    if (bytes.size() >= 8)
    {
        const auto magic = bigEndian(bytes, 0, 4);

        if (magic == fatMagic || magic == fat64Magic)
            return nesting == Nesting::anything
                       ? ltoKindOfUniversal(bytes, magic == fat64Magic)
                       : std::string {};
    }

    return ltoKindOfObject(bytes);
}
} // namespace

std::string ltoKindOf(std::string_view bytes)
{
    return ltoKindIn(bytes, Nesting::anything);
}
} // namespace epm
