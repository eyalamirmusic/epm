#include <eacp/Core/Core.h>

using namespace eacp;

// HelloEacp through a manifest: eacp is declared in epm.cpp and reached with
// find_package, whether it came from epm's binary cache or from source.
int main(int argc, char* argv[])
{
    return Apps::runConsole(argc,
                            argv,
                            []
                            {
                                Console::print("Hello from eacp, via epm.cpp");
                                return 0;
                            });
}
