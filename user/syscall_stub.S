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
    bl      user_main
    /* If user_main returns, call sys_exit */
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

/* void sys_exit(void) */
.global sys_exit
sys_exit:
    mov     x8, #1          /* SYS_EXIT */
    svc     #0
    b       .

/* void sys_yield(void) */
.global sys_yield
sys_yield:
    mov     x8, #2          /* SYS_YIELD */
    svc     #0
    ret
