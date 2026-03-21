#include "../ulib.h"

int main(const char *args)
{
    if (!args || !args[0]) {
        printf("usage: touch <file>\n");
        return 1;
    }
    int fd = sys_open(args, 4); /* O_CREATE */
    if (fd < 0) {
        printf("touch: cannot create %s\n", args);
        return 1;
    }
    sys_close(fd);
    return 0;
}
