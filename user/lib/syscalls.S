/*
 * syscalls.S -- All syscall stubs for userspace programs.
 */

.text

.global sys_write
sys_write:
    mov     x8, #0
    svc     #0
    ret

.global sys_exit
sys_exit:
    mov     x8, #1
    svc     #0
    b       .

.global sys_yield
sys_yield:
    mov     x8, #2
    svc     #0
    ret

.global sys_sleep
sys_sleep:
    mov     x8, #3
    svc     #0
    ret

.global sys_getc
sys_getc:
    mov     x8, #4
    svc     #0
    ret

.global sys_uptime
sys_uptime:
    mov     x8, #5
    svc     #0
    ret

.global sys_ps
sys_ps:
    mov     x8, #6
    svc     #0
    ret

.global sys_open
sys_open:
    mov     x8, #7
    svc     #0
    ret

.global sys_read
sys_read:
    mov     x8, #8
    svc     #0
    ret

.global sys_close
sys_close:
    mov     x8, #9
    svc     #0
    ret

.global sys_stat
sys_stat:
    mov     x8, #10
    svc     #0
    ret

.global sys_mkdir
sys_mkdir:
    mov     x8, #11
    svc     #0
    ret

.global sys_readdir
sys_readdir:
    mov     x8, #12
    svc     #0
    ret

.global sys_unlink
sys_unlink:
    mov     x8, #13
    svc     #0
    ret

.global sys_fork
sys_fork:
    mov     x8, #14
    svc     #0
    ret

.global sys_exec
sys_exec:
    mov     x8, #15
    svc     #0
    ret

.global sys_wait
sys_wait:
    mov     x8, #16
    svc     #0
    ret

.global sys_getpid
sys_getpid:
    mov     x8, #17
    svc     #0
    ret

.global sys_pipe
sys_pipe:
    mov     x8, #18
    svc     #0
    ret
