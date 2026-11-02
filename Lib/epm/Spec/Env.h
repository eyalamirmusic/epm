#pragma once

#include <eacp/Core/Utils/Containers.h>

#include <Miro/Reflect.h>

#include <string>

namespace epm
{
// env.json: the consumer's toolchain, written once per configure by epm.cmake.
// Unset CMake variables arrive as empty strings / arrays.
struct Env
{
    int schema = 1;
    std::string cmake;
    std::string cmakeVersion;
    std::string generator;
    bool isMultiConfig = false;
    std::string buildType;
    eacp::Vector<std::string> configurationTypes;
    std::string cCompiler;
    std::string cxxCompiler;
    std::string cxxCompilerId;
    std::string cxxCompilerVersion;
    std::string systemName;
    std::string systemProcessor;
    std::string osxArchitectures;
    std::string osxDeploymentTarget;
    std::string cxxStandard;
    std::string toolchainFile;

    MIRO_REFLECT(schema,
                 cmake,
                 cmakeVersion,
                 generator,
                 isMultiConfig,
                 buildType,
                 configurationTypes,
                 cCompiler,
                 cxxCompiler,
                 cxxCompilerId,
                 cxxCompilerVersion,
                 systemName,
                 systemProcessor,
                 osxArchitectures,
                 osxDeploymentTarget,
                 cxxStandard,
                 toolchainFile)
};
} // namespace epm
