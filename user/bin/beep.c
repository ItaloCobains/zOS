/*
 * beep.c -- Terminal beep sound.
 * Sends ASCII BEL character to the terminal.
 */

#include "../lib/ulib.h"

int main(const char *args)
{
    int count = 1;
    if (args && args[0] >= '1' && args[0] <= '9')
        count = args[0] - '0';

    for (int i = 0; i < count; i++) {
        /* ASCII BEL (0x07) -- terminal beeps */
        char bel = 7;
        sys_write(1, &bel, 1);
        if (i < count - 1)
            sys_sleep(30); /* 300ms between beeps */
    }
    printf("beep! (x%d)\n", count);
    return 0;
}
