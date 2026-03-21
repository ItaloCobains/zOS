#include "../lib/ulib.h"

int main(const char *args)
{
    (void)args;
    struct { int id; int state; unsigned long sleep_ticks; } info[8];
    int count = sys_ps(info, 8);

    printf("  PID  STATE\n");
    for (int i = 0; i < count; i++) {
        printf("  %d    ", info[i].id);
        switch (info[i].state) {
        case 1: printf("READY"); break;
        case 2: printf("RUNNING"); break;
        case 3: printf("SLEEPING"); break;
        case 5: printf("WAITING"); break;
        default: printf("?"); break;
        }
        printf("\n");
    }
    return 0;
}
