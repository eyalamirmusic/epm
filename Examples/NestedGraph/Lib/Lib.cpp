#include <Lib.h>

#include <eacp/Core/Core.h>

#include <string>

namespace Lib
{
void greet(std::string_view who)
{
    eacp::Console::print("Hello from " + std::string {who} + ", via Lib and eacp");
}
} // namespace Lib
