#include <epm/Spec/JsonFile.h>
#include <epm/Spec/Manifest.h>
#include <epm/Spec/Overrides.h>
#include <epm/Util/Text.h>

#include <eacp/Core/Utils/TemporaryFile.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
// Docs/Graph.md §1, plus a path-valued profile.
constexpr auto manifestFixture = R"({
  "schema": 1,
  "packages": [
    { "name": "fmt", "git": "fmtlib/fmt", "tag": "11.0.2",
      "options": { "FMT_TEST": "OFF" } },
    { "name": "B", "git": "org/B", "tag": "v2.1.0", "configs": ["Release"],
      "unknownKey": true }
  ],
  "profiles": {
    "ci": { "configs": ["Release"], "mode": "binary-only",
            "packages": [ { "name": "fmt", "mode": "source",
                            "options": { "X": "1" } } ] },
    "external": "policy/external.json"
  }
})";

constexpr auto externalProfile =
    R"({ "abiMatch": "strict", "packages": [ { "name": "B", "configs": ["Debug"] } ] })";

struct ManifestDirectory
{
    ManifestDirectory()
    {
        writeTextFile(manifestPath(), manifestFixture);
        writeTextFile(directory.path() / "policy" / "external.json",
                      externalProfile);
    }

    eacp::FilePath manifestPath() const { return directory.path() / "epm.json"; }

    Manifest manifest() const { return loadManifest(manifestPath()).manifest; }

    eacp::TemporaryFile directory;
};

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

auto tManifestLoads = test("Manifest/load/parsesContractFixture") = []
{
    const auto fixture = ManifestDirectory {};
    const auto loaded = loadManifest(fixture.manifestPath());

    check(loaded.ok);
    const auto& manifest = loaded.manifest;
    check(manifest.schema == 1);
    check(manifest.packages.size() == 2);

    // The file's order, not name order ("B" sorts before "fmt").
    const auto& fmt = manifest.packages[0];
    check(fmt.name == "fmt");
    check(fmt.git == "fmtlib/fmt");
    check(fmt.tag == "11.0.2");
    check(fmt.options.at("FMT_TEST") == "OFF");
    check(fmt.configs.empty());
    check(fmt.mode.empty());

    const auto& b = manifest.packages[1];
    check(b.name == "B");
    check(b.configs.size() == 1 && b.configs[0] == "Release");
    check(findPackage(manifest.packages, "B") == &b);
    check(findPackage(manifest.packages, "C") == nullptr);
    check(manifest.profiles.size() == 2);
    check(manifest.profiles.at("external").isString());
};

auto tManifestMissing = test("Manifest/load/missingOrMalformed") = []
{
    auto file = eacp::TemporaryFile {".json"};
    check(!loadManifest(file.path()).ok);

    writeTextFile(file.path(), "[1, 2]");
    check(!loadManifest(file.path()).ok);
};

auto tManifestDuplicate = test("Manifest/load/rejectsDuplicateAndEmptyNames") = []
{
    const auto errorOf = [](std::string_view json)
    { return manifestFromJSON(Miro::Json::parse(json)).error; };

    check(errorOf(R"({ "packages": [ { "name": "fmt", "tag": "1" },
        { "name": "B" }, { "name": "fmt", "tag": "2" } ] })")
          == "fmt is declared twice in packages");
    check(errorOf(R"({ "packages": [ { "name": "fmt" }, { "git": "org/B" } ] })")
          == "a package in packages has no name");
    check(errorOf(R"({ "packages": [ { "name": "fmt" } ], "profiles": {
        "ci": { "packages": [ { "name": "B" }, { "name": "B" } ] } } })")
          == "B is declared twice in the packages of profile ci");
    check(manifestFromJSON(Miro::Json::parse(R"({ "packages": [] })")).ok);
};

auto tManifestShorthand = test("Manifest/expandGitUrl") = []
{
    check(expandGitUrl("fmtlib/fmt") == "https://github.com/fmtlib/fmt.git");
    check(expandGitUrl("https://gitlab.com/a/b.git")
          == "https://gitlab.com/a/b.git");
    check(expandGitUrl("git@github.com:a/b.git") == "git@github.com:a/b.git");
    check(expandGitUrl("/srv/git/b") == "/srv/git/b");
    check(expandGitUrl("file:///srv/git/b") == "file:///srv/git/b");
    check(expandGitUrl("").empty());
};

auto tProfileObject = test("Manifest/profile/object") = []
{
    const auto fixture = ManifestDirectory {};
    const auto selection = selectProfile(
        fixture.manifest(), "ci", fixture.manifestPath().parentDirectory());

    check(selection.ok);
    check(selection.profile.mode == "binary-only");
    check(selection.profile.configs.size() == 1);
    check(selection.profile.packages.size() == 1);
    check(selection.profile.packages[0].name == "fmt");
    check(selection.profile.packages[0].mode == "source");
    check(selection.profile.packages[0].options.at("X") == "1");
};

auto tProfileFile = test("Manifest/profile/pathValueIsRelativeToManifest") = []
{
    const auto fixture = ManifestDirectory {};
    const auto selection = selectProfile(
        fixture.manifest(), "external", fixture.manifestPath().parentDirectory());

    check(selection.ok);
    check(selection.profile.abiMatch == "strict");
    check(findPackage(selection.profile.packages, "B")->configs[0] == "Debug");
};

auto tProfileFileDuplicate = test("Manifest/profile/fileRejectsDuplicateName") = []
{
    const auto fixture = ManifestDirectory {};
    writeTextFile(fixture.directory.path() / "policy" / "external.json",
                  R"({ "packages": [ { "name": "B" }, { "name": "B" } ] })");

    const auto selection = selectProfile(
        fixture.manifest(), "external", fixture.manifestPath().parentDirectory());

    check(!selection.ok);
    check(selection.error
          == "B is declared twice in the packages of profile external");
};

auto tProfileNameAsPath = test("Manifest/profile/nameMayBeAFile") = []
{
    const auto fixture = ManifestDirectory {};
    const auto path = fixture.directory.path() / "policy" / "external.json";
    const auto selection =
        selectProfile(fixture.manifest(), path.str(), eacp::FilePath {"/nowhere"});

    check(selection.ok);
    check(selection.profile.abiMatch == "strict");
};

auto tProfileUnknown = test("Manifest/profile/unknownAndEmpty") = []
{
    const auto fixture = ManifestDirectory {};
    const auto directory = fixture.manifestPath().parentDirectory();
    const auto unknown = selectProfile(fixture.manifest(), "nightly", directory);

    check(!unknown.ok);
    check(unknown.error.find("nightly") != std::string::npos);
    check(unknown.error.find("ci, external") != std::string::npos);

    const auto none = selectProfile(fixture.manifest(), "", directory);
    check(none.ok);
    check(none.profile.mode.empty());
};

auto tProfileBrokenFile = test("Manifest/profile/unreadableFile") = []
{
    auto manifest = Manifest {};
    manifest.profiles["gone"] = Miro::JSON {"missing.json"};

    check(!selectProfile(manifest, "gone", eacp::FilePath {"/nowhere"}).ok);
};

auto tOverridesParse = test("Overrides/parsesContractFixture") = []
{
    auto file = eacp::TemporaryFile {".json"};
    writeTextFile(file.path(), R"({
      "schema": 1, "profile": "ci", "mode": "", "configs": [], "abiMatch": "",
      "strict": true,
      "packages": { "fmt": { "mode": "source", "source": "/abs/fmt",
                             "configs": ["Debug"], "options": { "A": "1" } } } })");

    const auto overrides = loadJsonFile<Overrides>(file.path());

    check(overrides.has_value());
    check(overrides->profile == "ci");
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
                            {"EPM_PROFILE", "ci"},
                            {"EPM_CONFIGS", "Debug;Release"},
                            {"EPM_ABI_MATCH", "any"},
                            {"EPM_fmt_CONFIGS", "Release"},
                            {"EPM_fmt_MODE", "prebuilt"},
                            {"EPM_fmt_SOURCE", "/src/fmt"},
                            {"EPM_fmt_OPTIONS", "A=env;B=2;FLAG"},
                            {"EPM_other_MODE", "source"}});

    const auto overrides = withEnvironment(fromCMake, {"fmt"}, environment);

    check(overrides.mode == "source");
    check(overrides.profile == "ci");
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
