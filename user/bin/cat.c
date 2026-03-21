#include "../ulib.h"

int main(const char *args)
{
    if (!args || !args[0]) {
        printf("usage: cat <file>\n");
        return 1;
    }

    int fd = sys_open(args, 0);
    if (fd < 0) {
        printf("cat: cannot open %s\n", args);
        return 1;
    }

    char buf[256];
    int n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0)
        sys_write(1, buf, n);

    sys_close(fd);
    printf("\n");
    return 0;
}
