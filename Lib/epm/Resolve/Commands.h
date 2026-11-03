#pragma once

#include <epm/Spec/Env.h>

#include <eacp/Core/Utils/Containers.h>
#include <eacp/Core/Utils/FilePath.h>

#include <string>
#include <eacp/Core/Utils/Containers.h>

namespace epm
{
enum class Mode
{
    locked,
    update,
    offline
};

struct CommandOptions
{
    std::string command;
    EA::Vector<std::string> names;
    EA::Vector<std::string> packageArguments;
    Mode mode = Mode::locked;
    bool isCI = false;
    eacp::FilePath lock;
    eacp::FilePath cache;
    eacp::FilePath cmake {"cmake"};
    eacp::FilePath sourceDirectory;
    eacp::FilePath buildDirectory;
    eacp::FilePath requestedBy;
    eacp::FilePath requestedByFile;
    eacp::FilePath requestedBinaryDirectory;
    eacp::FilePath output;
    Env env;

    // MODE when the call names none: "auto" for epm_add_package, "source" for
    // the CPM and FetchContent commands, which always add_subdirectory.
    std::string defaultMode = "auto";
    bool buildMissingBinaries = true;
    EA::Vector<std::string> overrides;
    bool isFullVerify = false;
    bool shouldAnnounce = false;
};

int runBeginCommand(const CommandOptions& options);
int runAddCommand(const CommandOptions& options);
int runProvideCommand(const CommandOptions& options);
int runUpdateCommand(const CommandOptions& options);
int runVerifyCommand(const CommandOptions& options);
int runRootCommand(const CommandOptions& options);
} // namespace epm
