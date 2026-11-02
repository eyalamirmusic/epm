#include "Timestamp.h"

#include <chrono>
#include <format>

namespace epm
{
std::string utcTimestamp()
{
    using namespace std::chrono;
    return std::format("{:%FT%TZ}", floor<seconds>(system_clock::now()));
}
} // namespace epm
