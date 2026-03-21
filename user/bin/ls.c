#include "../lib/ulib.h"

int main(const char *args)
{
    const char *path = (args && args[0]) ? args : "/";

    int fd = sys_open(path, 0);
    if (fd < 0) {
        printf("ls: cannot open %s\n", path);
        return 1;
    }

    struct { int inode; int type; char name[32]; } entries[16];
    int count = sys_readdir(fd, entries, 16);
    sys_close(fd);

    for (int i = 0; i < count; i++) {
        if (entries[i].type == 2)
            printf("  %s/\n", entries[i].name);
        else if (entries[i].type == 3)
            printf("  %s  (device)\n", entries[i].name);
        else
            printf("  %s\n", entries[i].name);
    }
    return 0;
}
