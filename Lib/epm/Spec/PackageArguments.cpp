#include <epm/Spec/PackageArguments.h>

#include <eacp/Core/Utils/Strings.h>

#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>
#include <string_view>

namespace epm
{
namespace
{
enum class KeywordGroup
{
    value,
    flag,
    list,
    ignored,
    refused
};

struct KeywordRule
{
    std::string_view keyword;
    KeywordGroup group;
    std::string_view refusalReason = {};
};

constexpr auto commandReason =
    std::string_view {"runs a command whose result epm cannot lock or hash"};
constexpr auto otherSourceReason = std::string_view {
    "is a source epm cannot fetch; it locks git and archives"};

// Every keyword CPMAddPackage, FetchContent_Declare and ExternalProject_Add
// take. The ignored ones change how or where a fetch or sub-build runs, never
// which sources arrive: epm fetches exact commits, keys its cache by
// content and runs no sub-build. Anything not listed is refused as unknown.
constexpr auto keywordRules = std::to_array<KeywordRule>(
    {{"NAME", KeywordGroup::value},
     {"VERSION", KeywordGroup::value},
     {"GIT_TAG", KeywordGroup::value},
     {"GITHUB", KeywordGroup::value},
     {"GITHUB_REPOSITORY", KeywordGroup::value},
     {"GITLAB_REPOSITORY", KeywordGroup::value},
     {"BITBUCKET_REPOSITORY", KeywordGroup::value},
     {"GIT_REPOSITORY", KeywordGroup::value},
     {"SOURCE_DIR", KeywordGroup::value},
     {"SOURCE_SUBDIR", KeywordGroup::value},
     {"URI", KeywordGroup::value},
     {"URL_HASH", KeywordGroup::value},
     {"URL_MD5", KeywordGroup::value},
     {"GIT_SUBMODULES_RECURSE", KeywordGroup::value},
     {"MODE", KeywordGroup::value},
     {"DOWNLOAD_ONLY", KeywordGroup::flag},
     {"EXCLUDE_FROM_ALL", KeywordGroup::flag},
     {"SYSTEM", KeywordGroup::flag},
     {"OVERRIDE_FIND_PACKAGE", KeywordGroup::flag},
     {"EXTERNALPROJECT_INTERNAL_ARGUMENT_SEPARATOR", KeywordGroup::flag},
     {"URL", KeywordGroup::list},
     {"OPTIONS", KeywordGroup::list},
     {"PATCHES", KeywordGroup::list},
     {"GIT_SUBMODULES", KeywordGroup::list},
     {"FIND_PACKAGE_ARGS", KeywordGroup::list},
     {"QUIET", KeywordGroup::ignored},
     {"DOWNLOAD_NO_PROGRESS", KeywordGroup::ignored},
     {"DOWNLOAD_NAME", KeywordGroup::ignored},
     {"DOWNLOAD_EXTRACT_TIMESTAMP", KeywordGroup::ignored},
     {"DOWNLOAD_DIR", KeywordGroup::ignored},
     {"GIT_SHALLOW", KeywordGroup::ignored},
     {"GIT_PROGRESS", KeywordGroup::ignored},
     {"GIT_REMOTE_UPDATE_STRATEGY", KeywordGroup::ignored},
     {"GIT_REMOTE_NAME", KeywordGroup::ignored},
     {"GIT_CONFIG", KeywordGroup::ignored},
     {"NO_CACHE", KeywordGroup::ignored},
     {"CUSTOM_CACHE_KEY", KeywordGroup::ignored},
     {"FIND_PACKAGE_ARGUMENTS", KeywordGroup::ignored},
     {"UPDATE_DISCONNECTED", KeywordGroup::ignored},
     {"TLS_VERIFY", KeywordGroup::ignored},
     {"TLS_CAINFO", KeywordGroup::ignored},
     {"TLS_VERSION", KeywordGroup::ignored},
     {"TIMEOUT", KeywordGroup::ignored},
     {"INACTIVITY_TIMEOUT", KeywordGroup::ignored},
     {"NETRC", KeywordGroup::ignored},
     {"NETRC_FILE", KeywordGroup::ignored},
     {"HTTP_USERNAME", KeywordGroup::ignored},
     {"HTTP_PASSWORD", KeywordGroup::ignored},
     {"HTTP_HEADER", KeywordGroup::ignored},
     {"LOG_DOWNLOAD", KeywordGroup::ignored},
     {"LOG_UPDATE", KeywordGroup::ignored},
     {"LOG_PATCH", KeywordGroup::ignored},
     {"LOG_CONFIGURE", KeywordGroup::ignored},
     {"LOG_BUILD", KeywordGroup::ignored},
     {"LOG_INSTALL", KeywordGroup::ignored},
     {"LOG_TEST", KeywordGroup::ignored},
     {"LOG_MERGED_STDOUTERR", KeywordGroup::ignored},
     {"LOG_OUTPUT_ON_FAILURE", KeywordGroup::ignored},
     {"USES_TERMINAL_DOWNLOAD", KeywordGroup::ignored},
     {"USES_TERMINAL_UPDATE", KeywordGroup::ignored},
     {"USES_TERMINAL_PATCH", KeywordGroup::ignored},
     {"USES_TERMINAL_CONFIGURE", KeywordGroup::ignored},
     {"USES_TERMINAL_BUILD", KeywordGroup::ignored},
     {"USES_TERMINAL_INSTALL", KeywordGroup::ignored},
     {"USES_TERMINAL_TEST", KeywordGroup::ignored},
     {"PREFIX", KeywordGroup::ignored},
     {"TMP_DIR", KeywordGroup::ignored},
     {"STAMP_DIR", KeywordGroup::ignored},
     {"LOG_DIR", KeywordGroup::ignored},
     {"BINARY_DIR", KeywordGroup::ignored},
     {"SUBBUILD_DIR", KeywordGroup::ignored},
     {"INSTALL_DIR", KeywordGroup::ignored},
     {"CONFIGURE_COMMAND", KeywordGroup::ignored},
     {"BUILD_COMMAND", KeywordGroup::ignored},
     {"INSTALL_COMMAND", KeywordGroup::ignored},
     {"TEST_COMMAND", KeywordGroup::ignored},
     {"CMAKE_COMMAND", KeywordGroup::ignored},
     {"CMAKE_GENERATOR", KeywordGroup::ignored},
     {"CMAKE_GENERATOR_PLATFORM", KeywordGroup::ignored},
     {"CMAKE_GENERATOR_TOOLSET", KeywordGroup::ignored},
     {"CMAKE_GENERATOR_INSTANCE", KeywordGroup::ignored},
     {"CMAKE_ARGS", KeywordGroup::ignored},
     {"CMAKE_CACHE_ARGS", KeywordGroup::ignored},
     {"CMAKE_CACHE_DEFAULT_ARGS", KeywordGroup::ignored},
     {"CONFIGURE_HANDLED_BY_BUILD", KeywordGroup::ignored},
     {"BUILD_IN_SOURCE", KeywordGroup::ignored},
     {"BUILD_ALWAYS", KeywordGroup::ignored},
     {"BUILD_BYPRODUCTS", KeywordGroup::ignored},
     {"BUILD_JOB_SERVER_AWARE", KeywordGroup::ignored},
     {"INSTALL_BYPRODUCTS", KeywordGroup::ignored},
     {"TEST_BEFORE_INSTALL", KeywordGroup::ignored},
     {"TEST_AFTER_INSTALL", KeywordGroup::ignored},
     {"TEST_EXCLUDE_FROM_MAIN", KeywordGroup::ignored},
     {"DEPENDS", KeywordGroup::ignored},
     {"STEP_TARGETS", KeywordGroup::ignored},
     {"INDEPENDENT_STEP_TARGETS", KeywordGroup::ignored},
     {"LIST_SEPARATOR", KeywordGroup::ignored},
     {"COMMAND", KeywordGroup::ignored},
     {"EXCLUDE_FROM_ALL_STEPS", KeywordGroup::ignored},
     {"DOWNLOAD_COMMAND", KeywordGroup::refused, commandReason},
     {"UPDATE_COMMAND", KeywordGroup::refused, commandReason},
     {"PATCH_COMMAND",
      KeywordGroup::refused,
      "runs a command epm cannot hash; use PATCHES"},
     {"DOWNLOAD_NO_EXTRACT",
      KeywordGroup::refused,
      "keeps the archive unextracted, and epm locks extracted trees"},
     {"SVN_REPOSITORY", KeywordGroup::refused, otherSourceReason},
     {"HG_REPOSITORY", KeywordGroup::refused, otherSourceReason},
     {"CVS_REPOSITORY", KeywordGroup::refused, otherSourceReason},
     {"FORCE",
      KeywordGroup::refused,
      "would add a second version of a package, and the lock holds one"}});

constexpr auto hashAlgorithms = std::to_array<std::string_view>({"MD5",
                                                                 "SHA1",
                                                                 "SHA224",
                                                                 "SHA256",
                                                                 "SHA384",
                                                                 "SHA512",
                                                                 "SHA3_224",
                                                                 "SHA3_256",
                                                                 "SHA3_384",
                                                                 "SHA3_512"});

constexpr auto archiveExtensions = std::to_array<std::string_view>(
    {".zip", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tar"});

constexpr auto githubPrefix = std::string_view {"gh:"};
constexpr auto gitlabPrefix = std::string_view {"gl:"};
constexpr auto bitbucketPrefix = std::string_view {"bb:"};
constexpr auto gitExtension = std::string_view {".git"};

constexpr auto trueWords =
    std::to_array<std::string_view>({"YES", "ON", "TRUE", "1", "Y"});

constexpr auto falseWords =
    std::to_array<std::string_view>({"NO", "OFF", "FALSE", "0", "N", "IGNORE", ""});

template <std::size_t Size>
bool isOneOf(const std::array<std::string_view, Size>& words, std::string_view text)
{
    return std::find(words.begin(), words.end(), text) != words.end();
}

template <std::size_t Size>
bool isOneOfIgnoringCase(const std::array<std::string_view, Size>& words,
                         std::string_view text)
{
    return std::any_of(words.begin(),
                       words.end(),
                       [&](auto word)
                       { return eacp::Strings::equalsCaseInsensitive(word, text); });
}

std::optional<KeywordRule> findKeywordRule(std::string_view keyword)
{
    auto found =
        std::find_if(keywordRules.begin(),
                     keywordRules.end(),
                     [&](const auto& rule) { return rule.keyword == keyword; });

    if (found == keywordRules.end())
        return std::nullopt;

    return *found;
}

bool isKeyword(std::string_view text)
{
    return findKeywordRule(text).has_value();
}

bool isArchiveUrl(std::string_view url)
{
    return std::any_of(archiveExtensions.begin(),
                       archiveExtensions.end(),
                       [&](auto extension) { return url.ends_with(extension); });
}

struct KeywordValues
{
    std::map<std::string, std::string, std::less<>> values;
    std::map<std::string, std::vector<std::string>, std::less<>> lists;
    std::map<std::string, bool, std::less<>> flags;

    std::optional<std::string> findValue(std::string_view keyword) const
    {
        if (auto found = values.find(keyword); found != values.end())
            return found->second;

        return std::nullopt;
    }

    std::vector<std::string> findList(std::string_view keyword) const
    {
        if (auto found = lists.find(keyword); found != lists.end())
            return found->second;

        return {};
    }

    bool hasList(std::string_view keyword) const { return lists.contains(keyword); }

    bool isFlagSet(std::string_view keyword) const
    {
        auto found = flags.find(keyword);
        return found != flags.end() && found->second;
    }
};

KeywordValues collectKeywordValues(const std::vector<std::string>& arguments)
{
    auto collected = KeywordValues {};
    auto index = std::size_t {0};

    while (index < arguments.size())
    {
        const auto& keyword = arguments[index++];
        auto rule = findKeywordRule(keyword);

        if (!rule)
        {
            throw std::invalid_argument("epm does not know the option '"
                                        + keyword + "'");
        }

        if (rule->group == KeywordGroup::refused)
            throw std::invalid_argument(keyword + " "
                                        + std::string {rule->refusalReason});

        if (rule->group == KeywordGroup::flag)
        {
            auto isSet = true;

            if (index < arguments.size() && isCMakeBoolean(arguments[index]))
                isSet = !isCMakeFalse(arguments[index++]);

            collected.flags.emplace(keyword, isSet);
        }
        else if (rule->group == KeywordGroup::value)
        {
            if (index >= arguments.size())
                throw std::invalid_argument(keyword + " needs a value");

            collected.values.emplace(keyword, arguments[index++]);
        }
        else
        {
            auto list = std::vector<std::string> {};

            while (index < arguments.size() && !isKeyword(arguments[index]))
                list.push_back(arguments[index++]);

            if (rule->group == KeywordGroup::list)
                collected.lists.emplace(keyword, list);
        }
    }

    return collected;
}

std::vector<std::string> expandShorthand(std::string text)
{
    auto expanded = std::vector<std::string> {};
    auto tag = std::string {};
    auto version = std::string {};

    if (auto hash = text.rfind('#'); hash != std::string::npos)
    {
        tag = text.substr(hash + 1);
        text.resize(hash);
    }

    if (auto at = text.rfind('@'); at != std::string::npos && at > 0
                                   && text.find('/', at) == std::string::npos
                                   && text[at - 1] != ':')
    {
        version = text.substr(at + 1);
        text.resize(at);
    }

    if (text.starts_with(githubPrefix))
        expanded = {"GITHUB_REPOSITORY", text.substr(githubPrefix.size())};
    else if (text.starts_with(gitlabPrefix))
        expanded = {"GITLAB_REPOSITORY", text.substr(gitlabPrefix.size())};
    else if (text.starts_with(bitbucketPrefix))
        expanded = {"BITBUCKET_REPOSITORY", text.substr(bitbucketPrefix.size())};
    else if (isArchiveUrl(text))
        expanded = {"URL", text};
    else
        expanded = {"GIT_REPOSITORY", text};

    if (!version.empty())
        expanded.insert(expanded.end(), {"VERSION", version});

    if (!tag.empty())
        expanded.insert(expanded.end(), {"GIT_TAG", tag});

    return expanded;
}

std::vector<std::string> expandArguments(const std::vector<std::string>& arguments)
{
    if (arguments.size() == 1 && !isKeyword(arguments[0]))
        return expandShorthand(arguments[0]);

    auto expanded = std::vector<std::string> {};

    for (auto index = std::size_t {0}; index < arguments.size(); ++index)
    {
        if (arguments[index] == "URI" && index + 1 < arguments.size())
        {
            auto uriArguments = expandShorthand(arguments[++index]);
            expanded.insert(
                expanded.end(), uriArguments.begin(), uriArguments.end());
            continue;
        }

        expanded.push_back(arguments[index]);
    }

    return expanded;
}

ArchiveHash parseUrlHash(const std::string& urlHash)
{
    auto equals = urlHash.find('=');
    auto algorithm = eacp::Strings::toUpper(urlHash.substr(0, equals));

    if (equals == std::string::npos || !isOneOf(hashAlgorithms, algorithm))
    {
        throw std::invalid_argument("URL_HASH " + urlHash
                                    + " is not <algorithm>=<hex> with an "
                                      "algorithm CMake's file(DOWNLOAD) checks");
    }

    return {algorithm, eacp::Strings::toLower(urlHash.substr(equals + 1))};
}

// "NAME VALUE", or "NAME" alone meaning ON, as CPM's cpm_parse_option reads it.
std::pair<std::string, std::string> splitOption(const std::string& option)
{
    auto parts = eacp::Strings::split(option, ' ', 2);

    if (parts.size() < 2)
        return {option, "ON"};

    return {parts[0], eacp::Strings::trim(parts[1])};
}

void setSourceFromKeywords(PackageSpec& spec, const KeywordValues& keywords)
{
    auto github = keywords.findValue("GITHUB_REPOSITORY");

    if (!github)
        github = keywords.findValue("GITHUB");

    if (github)
    {
        spec.kind = PackageKind::git;
        spec.url = "https://github.com/" + *github + ".git";
    }
    else if (auto gitlab = keywords.findValue("GITLAB_REPOSITORY"))
    {
        spec.kind = PackageKind::git;
        spec.url = "https://gitlab.com/" + *gitlab + ".git";
    }
    else if (auto bitbucket = keywords.findValue("BITBUCKET_REPOSITORY"))
    {
        spec.kind = PackageKind::git;
        spec.url = "https://bitbucket.org/" + *bitbucket + ".git";
    }
    else if (auto repository = keywords.findValue("GIT_REPOSITORY"))
    {
        spec.kind = PackageKind::git;
        spec.url = *repository;
    }
    else if (auto urls = keywords.findList("URL"); !urls.empty())
    {
        spec.kind = PackageKind::archive;
        spec.url = urls.front();
    }
    else if (auto local = keywords.findValue("SOURCE_DIR"))
    {
        spec.kind = PackageKind::local;
        spec.localSourceDirectory = *local;
        return;
    }
    else
    {
        throw std::invalid_argument("a package needs GITHUB, GITHUB_REPOSITORY, "
                                    "GITLAB_REPOSITORY, BITBUCKET_REPOSITORY, "
                                    "GIT_REPOSITORY, URL or SOURCE_DIR");
    }

    spec.checkoutDirectory = keywords.findValue("SOURCE_DIR").value_or("");
}
} // namespace

std::vector<std::string_view> listIgnoredKeywords()
{
    auto ignored = std::vector<std::string_view> {};

    for (const auto& rule: keywordRules)
    {
        if (rule.group == KeywordGroup::ignored)
            ignored.push_back(rule.keyword);
    }

    return ignored;
}

bool isCMakeTrue(std::string_view value)
{
    return isOneOfIgnoringCase(trueWords, value);
}

bool isCMakeFalse(std::string_view value)
{
    return isOneOfIgnoringCase(falseWords, value);
}

bool isCMakeBoolean(std::string_view value)
{
    return isCMakeTrue(value) || isCMakeFalse(value);
}

std::string getPackageNameFromUrl(const std::string& url)
{
    auto path = url.substr(0, url.find_first_of("?#"));

    while (!path.empty() && path.back() == '/')
        path.pop_back();

    auto name = path.substr(path.find_last_of("/:") + 1);

    for (auto extension: archiveExtensions)
    {
        if (name.ends_with(extension))
            return name.substr(0, name.size() - extension.size());
    }

    if (name.ends_with(gitExtension))
        return name.substr(0, name.size() - gitExtension.size());

    return name;
}

PackageSpec parsePackageArguments(const std::vector<std::string>& arguments)
{
    auto keywords = collectKeywordValues(expandArguments(arguments));
    auto spec = PackageSpec {};

    setSourceFromKeywords(spec, keywords);

    spec.name = keywords.findValue("NAME").value_or("");

    if (spec.name.empty() && spec.kind != PackageKind::local)
        spec.name = getPackageNameFromUrl(spec.url);

    if (spec.name.empty())
        throw std::invalid_argument("a SOURCE_DIR package needs a NAME");

    spec.ref = keywords.findValue("GIT_TAG").value_or("");

    if (spec.kind == PackageKind::git && spec.ref.empty())
    {
        auto version = keywords.findValue("VERSION");

        if (!version)
            throw std::invalid_argument(spec.name + " needs a GIT_TAG or a VERSION");

        spec.ref = "v" + *version;
    }

    for (const auto& option: keywords.findList("OPTIONS"))
        spec.options.push_back(splitOption(option));

    spec.patches = keywords.findList("PATCHES");
    spec.sourceSubdirectory = keywords.findValue("SOURCE_SUBDIR").value_or("");
    spec.isDownloadOnly = keywords.isFlagSet("DOWNLOAD_ONLY");
    spec.isExcludedFromAll = keywords.isFlagSet("EXCLUDE_FROM_ALL");
    spec.isSystem = keywords.isFlagSet("SYSTEM");
    spec.submoduleSelection.isRestricted = keywords.hasList("GIT_SUBMODULES");
    spec.submoduleSelection.paths = keywords.findList("GIT_SUBMODULES");

    if (auto recurse = keywords.findValue("GIT_SUBMODULES_RECURSE"))
        spec.submoduleSelection.isRecursive = !isCMakeFalse(*recurse);

    spec.mode = keywords.findValue("MODE").value_or("");

    if (!spec.mode.empty() && spec.mode != "auto" && spec.mode != "prebuilt"
        && spec.mode != "source")
        throw std::invalid_argument("MODE must be auto, prebuilt or source, not '"
                                    + spec.mode + "'");

    if (auto urlHash = keywords.findValue("URL_HASH"))
        spec.archiveHash = parseUrlHash(*urlHash);
    else if (auto md5 = keywords.findValue("URL_MD5"))
        spec.archiveHash = parseUrlHash("MD5=" + *md5);

    return spec;
}
} // namespace epm
