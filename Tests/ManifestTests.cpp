#include <epm/Recipe/Project.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Spec/Manifest.h>
#include <epm/Spec/Overrides.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
EnvironmentLookup fixtureEnvironment(std::map<std::string, std::string> values)
{
    return [values = std::move(values)](std::string_view name)
    {
        const auto found = values.find(std::string {name});
        return found == values.end() ? std::optional<std::string> {}
                                     : std::optional {found->second};
    };
}
} // namespace

auto tProjectAdd = test("Manifest/project/addKeepsDeclarationOrder") = []
{
    auto project = Project {};
    setOption(project.add("fmt", "fmtlib/fmt", "11.0.2"), "FMT_TEST", "OFF");
    addConfig(project.add("B", "org/B", "v2.1.0"), "Release");

    // The order of the adds, not name order ("B" sorts before "fmt").
    check(project.packages.size() == 2);

    const auto& fmt = project.packages[0];
    check(fmt.name == "fmt");
    check(fmt.git == "fmtlib/fmt");
    check(fmt.tag == "11.0.2");
    check(fmt.options.at("FMT_TEST") == "OFF");
    check(fmt.configs.empty());
    check(fmt.mode.empty());

    const auto& b = project.packages[1];
    check(b.name == "B");
    check(b.configs == eacp::Vector<std::string> {"Release"});
    check(findPackage(project.packages, "B") == &b);
    check(findPackage(project.packages, "C") == nullptr);
};

auto tProjectHelpersChain = test("Manifest/project/helpersChain") = []
{
    auto project = Project {};
    auto& c = setAbiMatch(
        setMode(addConfig(addConfig(project.add("C"), "Debug"), "Release"),
                "source"),
        "strict");

    check(&c == &project.packages[0]);
    check(c.name == "C");
    check(c.git.empty() && c.tag.empty());
    check(c.configs == eacp::Vector<std::string> {"Debug", "Release"});
    check(c.mode == "source");
    check(c.abiMatch == "strict");

    setOption(c, "A", "1");
    setOption(c, "A", "2");
    check(c.options.size() == 1 && c.options.at("A") == "2");
};

auto tProjectError = test("Manifest/project/rejectsDuplicateAndEmptyNames") = []
{
    auto duplicate = Project {};
    duplicate.add("fmt", "fmtlib/fmt", "1");
    duplicate.add("B");
    duplicate.add("fmt", "fmtlib/fmt", "2");
    check(projectError(duplicate) == "fmt is declared twice in packages");

    auto unnamed = Project {};
    unnamed.add("fmt");
    unnamed.add("", "org/B");
    check(projectError(unnamed) == "a package in packages has no name");

    check(!projectError(Project {}).has_value());

    auto fine = Project {};
    fine.add("fmt", "fmtlib/fmt", "1");
    fine.add("B");
    check(!projectError(fine).has_value());
};

auto tProjectJson = test("Manifest/project/printsAsJson") = []
{
    auto project = Project {};
    setMode(project.add("fmt", "fmtlib/fmt", "11.0.2"), "source");

    const auto json = Miro::toJSONString(project);
    const auto back = Miro::createFromJSON<Project>(Miro::Json::parse(json));

    check(back.packages.size() == 1);
    check(back.packages[0].name == "fmt");
    check(back.packages[0].mode == "source");
};

auto tManifestShorthand = test("Manifest/expandGitUrl") = []
{
    check(expandGitUrl("fmtlib/fmt") == "https://github.com/fmtlib/fmt.git");
    check(expandGitUrl("https://gitlab.com/a/b.git")
          == "https://gitlab.com/a/b.git");
    check(expandGitUrl("git@github.com:a/b.git") == "git@github.com:a/b.git");
    check(expandGitUrl("/srv/git/b") == "/srv/git/b");
    check(expandGitUrl("C:/git/b") == "C:/git/b");
    check(expandGitUrl("c:\\git\\b") == "c:\\git\\b");
    check(isLocalRepository("D:/x"));
    check(!isLocalRepository("D:x"));
    check(!isLocalRepository("fmtlib/fmt"));
    check(expandGitUrl("file:///srv/git/b") == "file:///srv/git/b");
    check(expandGitUrl("").empty());
};

auto tManifestRepositoryKey = test("Manifest/repositoryKey/remoteSpellings") = []
{
    const auto key = std::string {"github.com/fmtlib/fmt"};

    for (const auto* url: {"https://github.com/fmtlib/fmt.git",
                           "https://github.com/fmtlib/fmt",
                           "https://github.com/FmtLib/fmt/",
                           "https://GitHub.com/fmtlib/fmt.git/",
                           "http://github.com/fmtlib/fmt",
                           "git://github.com/fmtlib/fmt.git",
                           "git@github.com:fmtlib/fmt.git",
                           "git@github.com:fmtlib/fmt",
                           "github.com:fmtlib/fmt.git",
                           "ssh://git@github.com/fmtlib/fmt",
                           "ssh://github.com/fmtlib/fmt.git",
                           "https://user:token@github.com/fmtlib/fmt.git"})
        check(repositoryKey(url) == key);
};

auto tManifestRepositoryKeyDiffers = test("Manifest/repositoryKey/distinct") = []
{
    const auto key = repositoryKey("https://github.com/fmtlib/fmt.git");

    check(repositoryKey("https://gitlab.com/fmtlib/fmt.git") != key);
    check(repositoryKey("git@gitlab.com:fmtlib/fmt.git") != key);
    check(repositoryKey("https://github.com/other/fmt.git") != key);
    check(repositoryKey("https://github.com/fmtlib/fmt2.git") != key);
    check(repositoryKey("https://github.com/fmtlib/fmt/sub") != key);
    check(repositoryKey("ssh://git@github.com:2222/fmtlib/fmt") != key);
};

auto tManifestRepositoryKeyLocal = test("Manifest/repositoryKey/localPaths") = []
{
    const auto key = repositoryKey("/srv/Git/fmt");

    check(repositoryKey("/srv/Git/fmt.git") == key);
    check(repositoryKey("/srv/Git/fmt/") == key);
    check(repositoryKey("file:///srv/Git/fmt.git") == key);
    check(repositoryKey("/srv/git/fmt") != key);

    // Elsewhere than Windows a drive-letter path is not absolute, so it is
    // kept as written; on Windows "/srv" gets the current drive.
    if (!eacp::FilePath {"C:/Git"}.isAbsolute())
    {
        check(key == "/srv/Git/fmt");
        check(repositoryKey("C:/Git/fmt.git") == "C:/Git/fmt");
        check(repositoryKey("file:///C:/Git/fmt.git") == "C:/Git/fmt");
    }

    // An existing directory is resolved: its symlinks and "..".
    const auto directory = eacp::TemporaryFile {"-repo"};
    check(eacp::Files::createDirectories(directory.path() / "Repo.git"));

    const auto resolved = repositoryKey((directory.path() / "Repo.git").str());
    check(resolved.ends_with("/Repo"));
    check(repositoryKey((directory.path() / "x" / ".." / "Repo.git").str())
          == resolved);
    check(repositoryKey("file://" + (directory.path() / "Repo.git").str())
          == resolved);
};

auto tOverridesParse = test("Overrides/parsesContractFixture") = []
{
    auto file = eacp::TemporaryFile {".json"};
    writeTextFile(file.path(), R"({
      "schema": 1, "mode": "", "configs": [], "abiMatch": "", "strict": true,
      "packages": { "fmt": { "mode": "source", "source": "/abs/fmt",
                             "configs": ["Debug"], "options": { "A": "1" } } } })");

    const auto overrides = loadJsonFile<Overrides>(file.path());

    check(overrides.has_value());
    check(overrides->strict);
    check(!overrides->buildMissingBinaries.has_value());
    check(overrides->packages.at("fmt").source == "/abs/fmt");
    check(overrides->packages.at("fmt").options.at("A") == "1");
};

auto tOverridesCMakeWins = test("Overrides/cmakeWinsOverEnvironment") = []
{
    auto fromCMake = Overrides {};
    fromCMake.mode = "source";
    fromCMake.packages["fmt"].configs = {"Debug"};
    fromCMake.packages["fmt"].options = {{"A", "cmake"}};

    const auto environment =
        fixtureEnvironment({{"EPM_MODE", "prebuilt"},
                            {"EPM_CONFIGS", "Debug;Release"},
                            {"EPM_ABI_MATCH", "any"},
                            {"EPM_fmt_CONFIGS", "Release"},
                            {"EPM_fmt_MODE", "prebuilt"},
                            {"EPM_fmt_SOURCE", "/src/fmt"},
                            {"EPM_fmt_OPTIONS", "A=env;B=2;FLAG"},
                            {"EPM_other_MODE", "source"}});

    const auto overrides = withEnvironment(fromCMake, {"fmt"}, environment);

    check(overrides.mode == "source");
    check(overrides.configs.size() == 2 && overrides.configs[1] == "Release");
    check(overrides.abiMatch == "any");

    const auto& fmt = overrides.packages.at("fmt");
    check(fmt.configs.size() == 1 && fmt.configs[0] == "Debug");
    check(fmt.mode == "prebuilt");
    check(fmt.source == "/src/fmt");
    check(fmt.options.at("A") == "cmake");
    check(fmt.options.at("B") == "2");
    check(fmt.options.at("FLAG") == "ON");

    // Only the names asked for are looked up.
    check(!overrides.packages.contains("other"));
};

auto tOverridesBools = test("Overrides/strictAndBuildMissingFromEnvironment") = []
{
    const auto environment = fixtureEnvironment(
        {{"EPM_STRICT", "ON"}, {"EPM_BUILD_MISSING_BINARIES", "off"}});
    const auto fromEnvironment = withEnvironment({}, {}, environment);

    check(fromEnvironment.strict);
    check(fromEnvironment.buildMissingBinaries == false);

    auto fromCMake = Overrides {};
    fromCMake.buildMissingBinaries = true;
    check(withEnvironment(fromCMake, {}, environment).buildMissingBinaries == true);

    const auto unset = withEnvironment({}, {"fmt"}, fixtureEnvironment({}));
    check(!unset.strict);
    check(!unset.buildMissingBinaries.has_value());
    check(unset.packages.empty());
};

auto tOverridesOptionList = test("Overrides/parseOptionList") = []
{
    const auto options = parseOptionList("A=1;B=x=y;;C");

    check(options.size() == 3);
    check(options.at("A") == "1");
    check(options.at("B") == "x=y");
    check(options.at("C") == "ON");
};
