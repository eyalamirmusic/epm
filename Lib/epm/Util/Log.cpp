#include "Log.h"

#include <eacp/Core/Utils/Console.h>

#include <atomic>
#include <cstdio>
#include <string>

namespace epm::Log
{
namespace
{
std::atomic<bool>& verboseFlag()
{
    static auto flag = std::atomic<bool> {false};
    return flag;
}
} // namespace

void setVerbose(bool verbose)
{
    verboseFlag() = verbose;
}

bool isVerbose()
{
    return verboseFlag();
}

void step(std::string_view line)
{
    if (isVerbose())
        eacp::Console::print(line);
}

void progress(std::string_view line)
{
    eacp::Console::print("-- " + std::string {line});
}

void stream(std::string_view chunk)
{
    if (!isVerbose())
        return;

    std::fwrite(chunk.data(), 1, chunk.size(), stdout);
    std::fflush(stdout);
}
} // namespace epm::Log
