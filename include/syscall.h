#ifndef ZOS_SYSCALL_H
#define ZOS_SYSCALL_H

#include "trap.h"

/* Syscall numbers */
#define SYS_WRITE  0
#define SYS_EXIT   1
#define SYS_YIELD  2

void syscall_handler(struct trap_frame *frame);

#endif
