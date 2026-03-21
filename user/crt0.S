/*
 * crt0.S -- C runtime startup for userspace programs.
 * Calls main() and then sys_exit.
 */

.section .text.boot
.global _start
_start:
    bl      main
    mov     x8, #1          /* SYS_EXIT */
    svc     #0
    b       .
