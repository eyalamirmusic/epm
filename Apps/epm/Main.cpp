#include <epm/Cache/CacheRoot.h>

#include <eacp/Core/App/App.h>
#include <eacp/Core/App/Args.h>
#include <eacp/Core/Utils/Console.h>

using namespace eacp;

namespace
{
constexpr auto usage = "usage: epm <command>\n"
                       "\n"
                       "commands:\n"
                       "  resolve      resolve one package request for epm.cmake\n"
                       "  cache path   print the cache root\n"
                       "  --version    print the version";

Vector<std::string> argumentsAfter(int count)
{
    const auto& all = Apps::commandLineArgs();
    auto rest = Vector<std::string> {};

    for (auto i = count; i < all.size(); ++i)
        rest.add(all[i]);

    return rest;
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

    Console::printError("epm resolve: not implemented");
    return 1;
}

int cache()
{
    const auto rest = argumentsAfter(2);

    if (rest.size() == 1 && rest[0] == "path")
    {
        Console::print(epm::CacheRoot::fromEnvironment().path().str());
        return 0;
    }

    Console::printError("usage: epm cache path");
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
        Console::print(EPM_VERSION);
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
