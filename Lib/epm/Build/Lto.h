#pragma once

#include <string>
#include <string_view>

namespace epm
{
// Link-time-optimisation objects hold compiler IR instead of machine code,
// and only the compiler that wrote them (its LTO plugin or libLTO) can link
// them: a config holding any is pinned to that compiler (Docs/PoC.md §9).
constexpr auto llvmBitcode = std::string_view {"llvm-bitcode"};
constexpr auto gccLto = std::string_view {"gcc-lto"};
constexpr auto msvcLtcg = std::string_view {"msvc-ltcg"};

// What kind of LTO objects `bytes` (a whole file) holds: llvmBitcode, gccLto,
// msvcLtcg or empty for none. It reads an object file (raw LLVM bitcode,
// `BC 0xC0DE`, or its wrapper, magic 0x0B17C0DE; a GCC object carrying
// `.gnu.lto_` sections in ELF or COFF, or a __GNU_LTO segment in Mach-O; an
// MSVC /GL object, the anonymous COFF object whose class ID is LTCG's), every
// member of an ar archive (GNU, BSD and Windows .lib spellings; thin archives
// hold no members), and every slice of a Mach-O universal file. A slice is an
// archive or an object and a member an object, so no input nests deeper,
// and a slice must lie after the universal header, inside the file.
std::string ltoKindOf(std::string_view bytes);
} // namespace epm
