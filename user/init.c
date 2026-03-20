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
    print("Hello from zOS userspace!\n");
    print("Running in EL0 (user mode).\n");

    /* Yield a few times to demonstrate scheduling */
    for (int i = 0; i < 3; i++) {
        print("Yielding...\n");
        sys_yield();
    }

    print("Userspace program done. Exiting.\n");
}
