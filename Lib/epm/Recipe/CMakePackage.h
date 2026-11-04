#pragma once

#include "Package.h"

namespace epm
{
// The default steps, Package::build()'s CMake flow, with hooks: derive from
// it to adjust the build of a package a recipe declares with add<T>().
//
//     struct Fmt : CMakePackage
//     {
//         void configure(CMakeBuild& build) override
//         {
//             build.option("FMT_DOC", "OFF");
//         }
//     };
class CMakePackage : public Package
{
public:
    void build() override;

    // Called once, before the fingerprint, on the build that starts from
    // spec().options: what it sets is part of the entry's fingerprint. It
    // runs whether or not the entry needs building, and in source mode too,
    // where the options it sets are those the consumer's add_subdirectory
    // gets (graph.cmake). Does nothing by default.
    virtual void configure(CMakeBuild& build);

    // Called after the missing configs are built into `prefix`, before they
    // are checked into the entry: to add files to the prefix. When it throws,
    // none of them is, and the next run builds them again. Does nothing by
    // default.
    virtual void afterBuild(const Prefix& prefix);
};

// The default steps, a plain CMakePackage, run in this process: what `epm
// resolve` runs for a package, and `epm configure` for one whose step has no
// recipe executable to run in (a root without an epm.cpp, Windows).
BuildOutput buildInProcess(const BuildInput& input);
} // namespace epm
