#include <DependencyResolution/Util/Status.h>

#include <eacp/Core/Utils/Console.h>

namespace DependencyResolution
{
void printStatus(const std::string& line)
{
    eacp::Console::printError("epm: " + line);
}
} // namespace DependencyResolution
