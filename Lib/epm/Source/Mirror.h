#pragma once

#include <eacp/Core/Process/Process.h>
#include <eacp/Core/Utils/FilePath.h>

#include <optional>
#include <string>
#include <eacp/Core/Utils/Containers.h>

namespace epm
{
// git's trimmed stdout, or nullopt when it cannot be launched or exits
// non-zero, in which case its stderr has been printed.
std::optional<std::string>
    runGit(const EA::Vector<std::string>& arguments,
           const EA::Vector<eacp::Processes::EnvironmentVariable>& environment = {});

void createBareMirror(const eacp::FilePath& mirror, const std::string& url);

bool hasCommit(const eacp::FilePath& mirror, const std::string& commit);

// The commit `ref` names on the remote now: a branch, a tag or a full sha.
std::string fetchRefIntoMirror(const eacp::FilePath& mirror, const std::string& ref);

enum class GitLfsFiles
{
    pointers,
    content
};

struct GitLink
{
    std::string path;
    std::string commit;
};

// One `ls-tree -r` of `commit`: its gitlinks and its .gitattributes files.
struct GitTreeListing
{
    eacp::FilePath mirror;
    std::string commit;
    EA::Vector<GitLink> gitlinks;
    EA::Vector<std::string> attributeFiles;
};

GitTreeListing listTree(const eacp::FilePath& mirror, const std::string& commit);

// True when any .gitattributes in the listing routes files through lfs.
bool usesGitLfs(const GitTreeListing& listing);

// Fetches every lfs object `commit` needs into the mirror; throws, naming the
// package to install, when git-lfs is missing.
void fetchGitLfsObjects(const eacp::FilePath& mirror, const std::string& commit);

void checkOutCommit(const eacp::FilePath& mirror,
                    const std::string& commit,
                    const eacp::FilePath& destination,
                    GitLfsFiles lfsFiles);

struct GitSubmodule
{
    std::string path;
    std::string url;
    std::string commit;
};

// The gitlinks in the listing that `.gitmodules` in `tree` gives a URL, each
// with the commit it names. A .gitmodules entry with no gitlink is skipped, as
// git does.
EA::Vector<GitSubmodule> listSubmodules(const GitTreeListing& listing,
                                        const eacp::FilePath& tree);

// The patch.exe Git for Windows installs, or an empty path when git is missing.
eacp::FilePath findGitBundledPatch();
} // namespace epm
