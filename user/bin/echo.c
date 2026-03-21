#include "../lib/ulib.h"

int main(const char *args)
{
    if (args && args[0])
        printf("%s\n", args);
    return 0;
}
