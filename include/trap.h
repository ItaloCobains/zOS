#ifndef ZOS_TRAP_H
#define ZOS_TRAP_H

#include "types.h"

/*
 * Saved register state when entering the kernel from an exception.
 * This matches the layout pushed/popped by vectors.S.
 */
struct trap_frame {
  uint64_t regs[31]; /* x0-x30 */
  uint64_t sp;       /* saved stack pointer (SP_EL0 for userspace) */
  uint64_t elr;      /* exception link register (return address) */
  uint64_t spsr;     /* saved program status register */
};

void trap_irq(struct trap_frame *frame);
void trap_sync(struct trap_frame *frame);
void trap_error(struct trap_frame *frame);

#endif
