#include "../lib/ulib.h"

int main(const char *args)
{
    (void)args;
    printf("Hello from zOS! PID=%d\n", sys_getpid());
    return 0;
}
