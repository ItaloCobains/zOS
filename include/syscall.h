#ifndef ZOS_SYSCALL_H
#define ZOS_SYSCALL_H

#include "trap.h"

/* Syscall numbers */
#define SYS_WRITE 0
#define SYS_EXIT 1
#define SYS_YIELD 2
#define SYS_SLEEP 3
#define SYS_GETC 4
#define SYS_UPTIME 5
#define SYS_PS 6
#define SYS_OPEN    7
#define SYS_READ    8
#define SYS_CLOSE   9
#define SYS_STAT   10
#define SYS_MKDIR  11
#define SYS_READDIR 12
#define SYS_UNLINK 13
#define SYS_FORK   14
#define SYS_EXEC   15
#define SYS_WAIT   16
#define SYS_GETPID 17

void syscall_handler(struct trap_frame *frame);

#endif
