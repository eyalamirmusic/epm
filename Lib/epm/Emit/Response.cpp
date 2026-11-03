#include "Response.h"

namespace epm
{
std::string cmakeQuoted(std::string_view text)
{
    auto quoted = std::string {"\""};

    for (const auto c: text)
    {
        if (c == '\\' || c == '"' || c == '$')
            quoted += '\\';

        quoted += c;
    }

    return quoted + "\"";
}

std::string prebuiltTargetsText(const eacp::FilePath& prefix)
{
    // Imported targets are local to the directory that creates them; the new
    // ones are made global, as add_subdirectory'd targets would be.
    return "get_property(_epm_imported_before DIRECTORY PROPERTY IMPORTED_TARGETS)\n"
           "include("
           + cmakeQuoted((prefix / "epm-targets.cmake").str())
           + ")\n"
             "get_property(_epm_imported_after DIRECTORY PROPERTY IMPORTED_TARGETS)\n"
             "foreach (_epm_target IN LISTS _epm_imported_after)\n"
             "    if (NOT _epm_target IN_LIST _epm_imported_before)\n"
             "        set_property(TARGET ${_epm_target} PROPERTY IMPORTED_GLOBAL "
             "TRUE)\n"
             "    endif ()\n"
             "endforeach ()\n";
}
} // namespace epm
