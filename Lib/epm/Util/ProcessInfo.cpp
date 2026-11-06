#include "ProcessInfo.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <climits>
#include <csignal>
#include <unistd.h>
#endif

namespace epm
{
#if defined(_WIN32)
std::int64_t currentProcessId()
{
    return static_cast<std::int64_t>(GetCurrentProcessId());
}

bool isProcessAlive(std::int64_t processId)
{
    if (processId <= 0)
        return false;

    const auto process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(processId));

    if (process == nullptr)
        return GetLastError() == ERROR_ACCESS_DENIED;

    auto exitCode = DWORD {0};
    const auto known = GetExitCodeProcess(process, &exitCode);
    CloseHandle(process);
    return !known || exitCode == STILL_ACTIVE;
}

std::string hostName()
{
    char name[MAX_COMPUTERNAME_LENGTH + 1] = {};
    auto size = DWORD {sizeof(name)};

    if (!GetComputerNameA(name, &size))
        return {};

    return std::string {name, size};
}
#else
std::int64_t currentProcessId()
{
    return static_cast<std::int64_t>(getpid());
}

bool isProcessAlive(std::int64_t processId)
{
    if (processId <= 0 || processId > INT_MAX)
        return false;

    if (kill(static_cast<pid_t>(processId), 0) == 0)
        return true;

    return errno == EPERM;
}

std::string hostName()
{
    char name[256] = {};

    if (gethostname(name, sizeof(name) - 1) != 0)
        return {};

    return name;
}
#endif
} // namespace epm
