#ifndef ZOS_SYSCALL_H
#define ZOS_SYSCALL_H

#include "trap.h"

/* Syscall numbers */
#define SYS_WRITE  0
#define SYS_EXIT   1
#define SYS_YIELD  2
#define SYS_SLEEP  3
#define SYS_GETC   4

void syscall_handler(struct trap_frame *frame);

#endif
