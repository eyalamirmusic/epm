#pragma once

#include "Env.h"

#include <string>
#include <string_view>

namespace epm
{
// The runtime library epm defaults to on the MSVC ABI: static, for epm's own
// build, the recipe executables, a consumer that sets none (epm.cmake) and
// so its dependencies. CMake's own default is the DLL one.
constexpr auto staticMsvcRuntime = "MultiThreaded$<$<CONFIG:Debug>:Debug>";

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

// The consumer compiles with cl or clang-cl (Env::cxxCompilerFrontend): the
// MSVC ABI, where objects and archives link only when compiled against the
// same runtime library, and a debug runtime (_DEBUG) only with debug ones.
bool msvcAbi(const Env& env);

// The runtime library of the consumer's objects and every child build on the
// MSVC ABI: env.msvcRuntime, else staticMsvcRuntime; empty off the MSVC ABI.
std::string msvcRuntimeOf(const Env& env);

// The runtime is a debug one in a Debug build: it names Debug, outright
// (MultiThreadedDebug) or for that configuration (CMake's generator
// expression). MultiThreaded alone gives a Debug build the release runtime.
bool msvcRuntimeHasDebug(std::string_view runtime);
} // namespace epm
