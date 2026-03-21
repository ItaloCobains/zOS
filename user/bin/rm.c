#include "../ulib.h"

int main(const char *args)
{
    if (!args || !args[0]) {
        printf("usage: rm <file>\n");
        return 1;
    }
    if (sys_unlink(args) < 0) {
        printf("rm: cannot remove %s\n", args);
        return 1;
    }
    return 0;
}
