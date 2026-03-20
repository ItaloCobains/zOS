/*
 * init.c -- First userspace program for zOS.
 *
 * Runs in EL0 (user mode). Uses syscalls to communicate with the kernel.
 * This is the simplest possible program: prints a message and exits.
 */

/* Syscall declarations (implemented in syscall_stub.S) */
extern long sys_write(const char *buf, unsigned long len);
extern void sys_exit(void);
extern void sys_yield(void);
extern void sys_sleep(unsigned long ticks);
extern int  sys_getc(void);

/* Simple strlen since we have no standard library */
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

/*
 * user_main -- called from _start in syscall_stub.S.
 * When this returns, _start calls sys_exit automatically.
 */
void user_main(void)
{
    print("[task1] Hello! Sleeping 50 ticks (~500ms) between prints.\n");

    for (int i = 0; i < 5; i++) {
        print("[task1] awake!\n");
        sys_sleep(50);  /* sleep ~500ms (50 ticks * 10ms) */
    }

    print("[task1] done. Exiting.\n");
}
