#include <eacp/Core/Core.h>

using namespace eacp;

// The smallest consumer of eacp: whether eacp-core came from epm's binary cache
// or was added from source, this links against it and prints one line.
int main(int argc, char* argv[])
{
    return Apps::runConsole(argc,
                            argv,
                            []
                            {
                                Console::print("Hello from eacp, via epm");
                                return 0;
                            });
}
