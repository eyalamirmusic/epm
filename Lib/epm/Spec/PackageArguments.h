#pragma once

#include <epm/Spec/PackageKind.h>
#include <epm/Source/Submodules.h>

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <eacp/Core/Utils/Containers.h>

namespace epm
{
// URL_HASH <algorithm>=<hex>, in any algorithm CMake's file(<algorithm>) takes.
struct ArchiveHash
{
    std::string algorithm;
    std::string hex;
};

struct PackageSpec
{
    std::string name;
    PackageKind kind = PackageKind::git;
    std::string url;
    std::string ref;
    std::string localSourceDirectory;
    std::string checkoutDirectory;
    std::string sourceSubdirectory;
    EA::Vector<std::pair<std::string, std::string>> options;
    EA::Vector<std::string> patches;
    SubmoduleSelection submoduleSelection;
    ArchiveHash archiveHash;
    bool isDownloadOnly = false;
    bool isExcludedFromAll = false;
    bool isSystem = false;

    // epm's MODE: auto, prebuilt or source; empty when the call names none.
    std::string mode;
};

// The arguments of epm_add_package, CPMAddPackage (keyword or single-string
// "gh:owner/repo@1.2.3#tag" form, or URI <shorthand>) and FetchContent_Declare.
// SOURCE_DIR alone is a local checkout; beside a repository or URL it is where
// the fetched tree is put, as in CPM and FetchContent.
// Throws std::invalid_argument naming what is missing or malformed, and every
// option epm does not know or cannot honour.
PackageSpec parsePackageArguments(const EA::Vector<std::string>& arguments);

// The keywords accepted and ignored because they never change which sources
// are fetched: progress, logging, TLS, timeouts, sub-build steps and so on.
EA::Vector<std::string_view> listIgnoredKeywords();

// CMake's if() constants: ON/YES/TRUE/Y/1, and OFF/NO/FALSE/N/0/IGNORE or empty.
bool isCMakeTrue(std::string_view value);
bool isCMakeFalse(std::string_view value);
bool isCMakeBoolean(std::string_view value);

// "Miro" for "https://github.com/eyalamirmusic/Miro.git" or
// ".../Miro-1.0.zip": the last path component without its extension.
std::string getPackageNameFromUrl(const std::string& url);
} // namespace epm
