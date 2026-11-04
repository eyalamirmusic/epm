#pragma once

#include <ea_data_structures/Structures/Vector.h>

#include <Miro/Reflect.h>

#include <optional>
#include <string>
#include <string_view>

namespace epm
{
// What one project asks of a package: its URL, its ref and the commit that ref
// resolved to. A root project's request names it "root"; a dependency's, the
// dependency.
struct Requirement
{
    std::string requestedBy;
    std::string url;
    std::string requestedRef;
    std::string commit;

    MIRO_REFLECT(requestedBy, url, requestedRef, commit)
};

struct SemanticVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};

// "v1.2.3", "1.2" or "2", with no suffix; nullopt for anything else.
std::optional<SemanticVersion> parseSemanticVersion(std::string_view tag);

bool isOlderVersion(const SemanticVersion& first, const SemanticVersion& second);

struct RequirementSelection
{
    bool isConflict = false;
    Requirement selected;
};

// Go's minimal version selection, for refs: when every requirement is a
// semantic version tag of one URL, the highest; when all name one commit of
// one URL (an archive's commit is its hash, or empty), that commit; otherwise
// a conflict only the root epm.cpp can settle.
RequirementSelection selectRequirement(const EA::Vector<Requirement>& requirements);

bool isSameRequirement(const Requirement& first, const Requirement& second);

// Every requester and its ref, and the fix: an override in the root epm.cpp.
std::string describeRequirementConflict(const std::string& name,
                                        const EA::Vector<Requirement>& requirements);
} // namespace epm
