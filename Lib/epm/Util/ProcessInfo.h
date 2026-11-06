#pragma once

#include <cstdint>
#include <string>

// What epm needs to tell whether a process that left something behind is
// still running. (Upstream candidates: eacp has no current process id, host
// name or liveness check.)
namespace epm
{
std::int64_t currentProcessId();

// Whether a process with this id runs on this machine: false only when the
// system says there is none, true when it cannot tell (one of another user,
// say), so a caller deleting what a dead process left errs towards keeping it.
bool isProcessAlive(std::int64_t processId);

// This machine's host name; empty when the system does not say.
std::string hostName();
} // namespace epm
