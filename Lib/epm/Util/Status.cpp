#include <epm/Util/Status.h>

#include <eacp/Core/Utils/Console.h>

namespace epm
{
void printStatus(const std::string& line)
{
    eacp::Console::printError("epm: " + line);
}
} // namespace epm
