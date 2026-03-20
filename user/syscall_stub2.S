/*
 * syscall_stub.S -- Userspace syscall wrappers.
 *
 * Each function puts the syscall number in x8, arguments in x0-x5,
 * and executes `svc #0` to trap into the kernel.
 * The return value comes back in x0.
 */

.section .text.boot
.global _start
_start:
    bl      user_main2
    /* If user_main2 returns, call sys_exit */
    mov     x8, #1          /* SYS_EXIT */
    svc     #0
    b       .               /* Should never reach here */

.text

/* int64_t sys_write(const char *buf, uint64_t len) */
.global sys_write
sys_write:
    mov     x8, #0          /* SYS_WRITE */
    svc     #0
    ret

/* void sys_yield(void) */
.global sys_yield
sys_yield:
    mov     x8, #2          /* SYS_YIELD */
    svc     #0
    ret

/* void sys_sleep(uint64_t ticks) */
.global sys_sleep
sys_sleep:
    mov     x8, #3          /* SYS_SLEEP */
    svc     #0
    ret

/* int sys_getc(void) */
.global sys_getc
sys_getc:
    mov     x8, #4          /* SYS_GETC */
    svc     #0
    ret

/* uint64_t sys_uptime(void) */
.global sys_uptime
sys_uptime:
    mov     x8, #5          /* SYS_UPTIME */
    svc     #0
    ret

/* int sys_ps(void *buf, int max) */
.global sys_ps
sys_ps:
    mov x8, #6
    svc #0
    ret
