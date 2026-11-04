#include "Timestamp.h"

#include <chrono>
#include <format>

namespace epm
{
namespace
{
std::int64_t steadyMilliseconds()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

std::string utcTimestamp()
{
    using namespace std::chrono;
    return std::format("{:%FT%TZ}", floor<seconds>(system_clock::now()));
}

std::string formatDuration(std::int64_t seconds)
{
    if (seconds < 60)
        return std::format("{}s", seconds);

    if (seconds < 3600)
        return std::format("{}m {}s", seconds / 60, seconds % 60);

    return std::format("{}h {}m", seconds / 3600, seconds % 3600 / 60);
}

Stopwatch::Stopwatch()
    : start(steadyMilliseconds())
{
}

std::int64_t Stopwatch::elapsedSeconds() const
{
    return (steadyMilliseconds() - start) / 1000;
}
} // namespace epm
