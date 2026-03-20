/*
 * task2.c -- Echo program.
 * Reads characters from UART and prints them back.
 * Demonstrates sys_getc and runs alongside task1 (which uses sys_sleep).
 */

extern long sys_write(const char *buf, unsigned long len);
extern void sys_yield(void);
extern void sys_sleep(unsigned long ticks);
extern int  sys_getc(void);

static unsigned long strlen(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

static void print(const char *msg)
{
    sys_write(msg, strlen(msg));
}

void user_main2(void)
{
    print("[task2] echo: type something!\n");

    for (;;) {
        int c = sys_getc();
        if (c >= 0) {
            /* Got a character -- echo it back */
            char buf[2];
            buf[0] = (char)c;
            buf[1] = 0;
            if (c == '\r') {
                print("\n");  /* Enter key sends \r, print newline */
            } else {
                sys_write(buf, 1);
            }
        } else {
            /* Nothing to read -- yield so other tasks can run */
            sys_yield();
        }
    }
}
