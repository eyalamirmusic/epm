#include <reloc/Reloc.h>

int relocValue()
{
    return RELOC_VERSION + RELOC_GENERATED + relocDepValue();
}

const char* relocSourceFile()
{
    return __FILE__;
}
