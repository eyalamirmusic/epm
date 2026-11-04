#include <Lib.h>

// App uses only Lib; eacp comes with it, from source or from epm's binary
// cache, as App/epm.cpp decides.
int main()
{
    Lib::greet("App");
    return 0;
}
