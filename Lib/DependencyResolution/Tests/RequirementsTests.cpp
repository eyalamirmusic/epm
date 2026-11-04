#include <DependencyResolution/Lock/Requirements.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace DependencyResolution;

namespace RequirementsTests
{
constexpr auto sharedUrl = "https://example.com/shared.git";
constexpr auto firstCommit = "1111111111111111111111111111111111111111";
constexpr auto secondCommit = "2222222222222222222222222222222222222222";

Requirement createRequirement(const std::string& requester,
                              const std::string& ref,
                              const std::string& commit)
{
    return {requester, sharedUrl, ref, commit};
}
} // namespace RequirementsTests

auto tParsesVersions = test("Requirements/parsesSemanticVersions") = []
{
    check(parseSemanticVersion("v1.2.3").has_value());
    check(parseSemanticVersion("1.2").has_value());
    check(parseSemanticVersion("2").has_value());
    check(!parseSemanticVersion("main").has_value());
    check(!parseSemanticVersion("v1.2.3-rc1").has_value());
    check(!parseSemanticVersion("1.2.3.4").has_value());
    check(isOlderVersion(*parseSemanticVersion("v1.9"),
                         *parseSemanticVersion("v1.10")));
};

auto tHighestTag = test("Requirements/selectsTheHighestTag") = []
{
    using namespace RequirementsTests;
    const auto selection =
        selectRequirement({createRequirement("a", "v1.0.0", firstCommit),
                           createRequirement("b", "v1.1.0", secondCommit)});

    check(!selection.isConflict);
    check(selection.selected.requestedRef == "v1.1.0");
    check(selection.selected.requestedBy == "b");
};

auto tSameCommit = test("Requirements/acceptsOneCommitNamedTwoWays") = []
{
    using namespace RequirementsTests;
    const auto selection =
        selectRequirement({createRequirement("a", "main", firstCommit),
                           createRequirement("b", firstCommit, firstCommit)});

    check(!selection.isConflict);
    check(selection.selected.commit == firstCommit);
};

auto tConflicts = test("Requirements/conflictsOnCommitsAndUrls") = []
{
    using namespace RequirementsTests;
    check(selectRequirement({createRequirement("a", "main", firstCommit),
                             createRequirement("b", "develop", secondCommit)})
              .isConflict);

    auto otherUrl = createRequirement("b", "v1.0.0", firstCommit);
    otherUrl.url = "https://example.com/fork.git";
    check(
        selectRequirement({createRequirement("a", "v1.0.0", firstCommit), otherUrl})
            .isConflict);
};
