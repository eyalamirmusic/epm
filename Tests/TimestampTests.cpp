#include <epm/Util/Timestamp.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using epm::formatDuration;

auto tDurationSeconds = test("Timestamp/duration/seconds") = []
{
    check(formatDuration(0) == "0s");
    check(formatDuration(59) == "59s");
};

auto tDurationMinutes = test("Timestamp/duration/minutes") = []
{
    check(formatDuration(60) == "1m 0s");
    check(formatDuration(102) == "1m 42s");
    check(formatDuration(3599) == "59m 59s");
};

auto tDurationHours = test("Timestamp/duration/hours") = []
{
    check(formatDuration(3600) == "1h 0m");
    check(formatDuration(7530) == "2h 5m");
};
