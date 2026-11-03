#include <epm/Cache/CacheListing.h>
#include <epm/Cache/CacheRoot.h>
#include <epm/Emit/Response.h>
#include <epm/Resolve/Resolve.h>
#include <epm/Spec/JsonFile.h>
#include <epm/Util/Log.h>

#include "Version.h"

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Console.h>
#include <eacp/Core/Utils/Files.h>
#include <eacp/Core/Utils/StdPath.h>

#include <filesystem>

using namespace eacp;

namespace
{
constexpr auto usage = "usage: epm <command>\n"
                       "\n"
                       "commands:\n"
                       "  resolve      resolve one package request for epm.cmake\n"
                       "  cache path   print the cache root\n"
                       "  cache ls     list the cached binaries, sources and tools\n"
                       "  --version    print the version";

Vector<std::string> argumentsAfter(int count)
{
    const auto& all = Apps::commandLineArgs();
    auto rest = Vector<std::string> {};

    for (auto i = count; i < all.size(); ++i)
        rest.add(all[i]);

    return rest;
}

int fail(const std::string& message)
{
    Console::printError("epm: " + message);
    return 1;
}

// One spelling for every path epm writes into the cache and matches in export
// files, whatever symlinks (/tmp on macOS) the cache root was given through.
epm::CacheRoot canonical(const epm::CacheRoot& cache)
{
    Files::createDirectories(cache.path());
    auto failure = std::error_code {};
    const auto resolved =
        std::filesystem::weakly_canonical(toStdPath(cache.path()), failure);

    return failure ? cache : epm::CacheRoot {FilePath {resolved}};
}

int resolve()
{
    auto request = FilePath {};
    auto env = FilePath {};
    auto response = FilePath {};
    auto cache = FilePath {};
    auto verbose = false;

    auto cli = Args::Parser {"epm resolve", "Resolves one package request."};
    cli.option("--request", request, "the request JSON").required();
    cli.option("--env", env, "the env JSON").required();
    cli.option("--response", response, "the response CMake file to write")
        .required();
    cli.option("--cache", cache, "the cache root (default: $EPM_CACHE)");
    cli.flag("--verbose", verbose, "log each step");

    if (auto code = cli.exitCodeFor(cli.parse(argumentsAfter(2))))
        return *code;

    epm::Log::setVerbose(verbose);

    try
    {
        const auto envSpec = epm::loadJsonFile<epm::Env>(env);

        if (!envSpec)
            return fail("cannot read the env file " + env.str());

        const auto requestSpec = epm::loadJsonFile<epm::Request>(request);

        if (!requestSpec)
            return fail("cannot read the request file " + request.str());

        const auto cacheRoot = cache.empty() ? epm::CacheRoot::fromEnvironment()
                                             : epm::CacheRoot {cache};
        const auto result =
            epm::resolvePackage(*requestSpec, *envSpec, canonical(cacheRoot));

        if (!result.ok)
        {
            if (!result.details.empty())
                Console::printError(result.details);

            return fail(result.error);
        }

        if (!epm::writeResponse(response, result.response))
            return fail("cannot write " + response.str());
    }
    catch (const std::exception& error)
    {
        return fail(error.what());
    }

    return 0;
}

int cache()
{
    const auto rest = argumentsAfter(2);

    if (rest.size() == 1 && rest[0] == "path")
    {
        Console::print(epm::CacheRoot::fromEnvironment().path().str());
        return 0;
    }

    if (!rest.empty() && rest[0] == "ls")
    {
        auto root = FilePath {};
        auto cli = Args::Parser {"epm cache ls", "Lists what the cache holds."};
        cli.option("--cache", root, "the cache root (default: $EPM_CACHE)");

        if (auto code = cli.exitCodeFor(cli.parse(argumentsAfter(3))))
            return *code;

        const auto cacheRoot =
            root.empty() ? epm::CacheRoot::fromEnvironment() : epm::CacheRoot {root};
        Console::print(epm::cacheListing(canonical(cacheRoot)));
        return 0;
    }

    Console::printError(
        "usage: epm cache path\n       epm cache ls [--cache <dir>]");
    return Args::badArguments;
}

int run()
{
    const auto& args = Apps::commandLineArgs();

    if (args.size() < 2)
    {
        Console::printError(usage);
        return Args::badArguments;
    }

    const auto& command = args[1];

    if (command == "--version")
    {
        Console::print(epm::version);
        return 0;
    }

    if (command == "--help" || command == "-h")
    {
        Console::print(usage);
        return 0;
    }

    if (command == "resolve")
        return resolve();

    if (command == "cache")
        return cache();

    Console::printError("epm: unknown command '" + command + "'\n\n" + usage);
    return Args::badArguments;
}
} // namespace

int main(int argc, char* argv[])
{
    return Apps::runConsole(argc, argv, run);
}
