#include "../ulib.h"

int main(const char *args)
{
    if (!args || !args[0]) {
        printf("usage: mkdir <dir>\n");
        return 1;
    }
    if (sys_mkdir(args) < 0) {
        printf("mkdir: cannot create %s\n", args);
        return 1;
    }
    return 0;
}
