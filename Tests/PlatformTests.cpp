#include <epm/Spec/Platform.h>

#include <NanoTest/NanoTest.h>

using namespace nano;
using namespace epm;

namespace
{
Env platformEnv(std::string system,
                std::string processor,
                std::string architectures = {},
                std::string sysroot = {},
                std::string androidAbi = {})
{
    auto env = Env {};
    env.systemName = std::move(system);
    env.systemProcessor = std::move(processor);
    env.osxArchitectures = std::move(architectures);
    env.osxSysroot = std::move(sysroot);
    env.androidAbi = std::move(androidAbi);
    return env;
}
} // namespace

auto tPlatMacos = test("Platform/macos") = []
{
    check(platformName(platformEnv("Darwin", "arm64")) == "macos-arm64");
    check(platformName(platformEnv("Darwin", "x86_64")) == "macos-x64");
    check(platformName(platformEnv("Darwin", "arm64", "x86_64")) == "macos-x64");
    check(platformName(platformEnv("Darwin", "arm64", "arm64;x86_64"))
          == "macos-universal");
    check(platformName(platformEnv("Darwin", "x86_64", "x86_64;arm64"))
          == "macos-universal");
};

auto tPlatIos = test("Platform/ios") = []
{
    const auto device = "/Applications/Xcode.app/Contents/Developer/Platforms/"
                        "iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk";
    const auto simulator = "/Applications/Xcode.app/Contents/Developer/Platforms/"
                           "iPhoneSimulator.platform/Developer/SDKs/"
                           "iPhoneSimulator.sdk";

    check(platformName(platformEnv("iOS", "arm64", "arm64", device)) == "ios-arm64");
    check(platformName(platformEnv("iOS", "", "arm64", "iphoneos")) == "ios-arm64");
    check(platformName(platformEnv("iOS", "arm64", "arm64", simulator))
          == "ios-simulator-arm64");
    check(platformName(platformEnv("iOS", "x86_64", "x86_64", "iphonesimulator"))
          == "ios-simulator-x64");
};

auto tPlatWindows = test("Platform/windows") = []
{
    check(platformName(platformEnv("Windows", "AMD64")) == "windows-x64");
    check(platformName(platformEnv("Windows", "ARM64")) == "windows-arm64");
};

auto tPlatLinux = test("Platform/linux") = []
{
    check(platformName(platformEnv("Linux", "x86_64")) == "linux-x64");
    check(platformName(platformEnv("Linux", "aarch64")) == "linux-arm64");
};

auto tPlatAndroid = test("Platform/android") = []
{
    check(platformName(platformEnv("Android", "aarch64", {}, {}, "arm64-v8a"))
          == "android-arm64-v8a");
    check(platformName(platformEnv("Android", "x86_64", {}, {}, "x86_64"))
          == "android-x86_64");
};

auto tPlatOther = test("Platform/other") = []
{
    check(platformName(platformEnv("FreeBSD", "amd64")) == "freebsd-amd64");
    check(platformName(platformEnv("Emscripten", "x86")) == "emscripten-x86");
};
