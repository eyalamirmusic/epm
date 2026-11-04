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

    // A find_package call: locked mode writes a missing entry instead of
    // refusing it, and errors name the find_package line.
    bool isFindPackageRequest = false;
    eacp::FilePath lock;

    // The root project's epm.json; a missing file overrides nothing.
    eacp::FilePath manifest;

    // Who the top-level project's requirements are recorded as: "root", or
    // in a child build the package being built.
    std::string rootName = "root";
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

    // The epm.json graph's packages: a package call for one of them loads it
    // from the graph, so each name has one definition.
    EA::Vector<std::string> graphPackages;

    // configure: overrides.json, and where graph.cmake and graph.json go.
    eacp::FilePath overridesFile;
    eacp::FilePath outputDirectory;

    // EPM_CONFIGS as epm.cmake resolved it, and EPM_ABI_MATCH (Request).
    EA::Vector<std::string> configs;
    std::string abiMatch = "compatible";
    EA::Vector<std::string> overrides;
    bool isFullVerify = false;
    bool shouldAnnounce = false;
};

int runBeginCommand(const CommandOptions& options);
int runResolveCommand(const CommandOptions& options);
int runConfigureCommand(const CommandOptions& options);
int runProvideCommand(const CommandOptions& options);

// find_package of a name outside the graph: added from the lock, else from a
// requirement some package in the graph recorded; a missing lock entry is
// written in locked mode and refused under EPM_CI.
int runFindCommand(const CommandOptions& options);
int runUpdateCommand(const CommandOptions& options);
int runVerifyCommand(const CommandOptions& options);
int runRootCommand(const CommandOptions& options);
} // namespace epm
