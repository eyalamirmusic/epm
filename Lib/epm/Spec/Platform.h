#pragma once

#include "Env.h"

#include <string>

namespace epm
{
// The platform a consumer builds for, as one name: macos-universal,
// macos-arm64, macos-x64, ios-arm64, ios-simulator-arm64, ios-simulator-x64,
// ios-simulator-universal (tvos-, watchos- and visionos- alike), windows-x64,
// windows-arm64, linux-x64, linux-arm64, android-<abi>, else
// <system name>-<processor> in lower case. The binary cache groups entries by
// it; the five desktop names match get_epm.cmake's release asset keys.
std::string platformName(const Env& env);

// The consumer named its target system itself (CMAKE_CROSSCOMPILING) without a
// toolchain file, so a child build must be told the system, processor and
// sysroot, or it configures for the host. A toolchain file, which child builds
// get too, says them itself.
bool crossCompilesWithoutToolchain(const Env& env);
} // namespace epm
