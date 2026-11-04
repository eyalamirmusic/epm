#include "CMakePackage.h"

namespace epm
{
void CMakePackage::build()
{
    buildWithCMake([this](CMakeBuild& build) { configure(build); },
                   [this](const Prefix& prefix) { afterBuild(prefix); });
}

void CMakePackage::configure(CMakeBuild&) {}

void CMakePackage::afterBuild(const Prefix&) {}

BuildOutput buildInProcess(const BuildInput& input)
{
    auto package = CMakePackage {};
    return runPackage(package, input);
}
} // namespace epm
