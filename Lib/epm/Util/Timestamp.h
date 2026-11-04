#pragma once

#include <cstdint>
#include <string>

namespace epm
{
// Now, as ISO 8601 UTC to the second: "2026-11-02T10:52:00Z".
std::string utcTimestamp();

// A whole number of seconds, short: "7s", "1m 42s", "2h 5m".
std::string formatDuration(std::int64_t seconds);

// Steady-clock time since construction, for "built eacp in 1m 42s".
class Stopwatch
{
public:
    Stopwatch();

    std::int64_t elapsedSeconds() const;

private:
    std::int64_t start = 0; // steady-clock milliseconds
};
} // namespace epm
