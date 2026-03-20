/*
 * syscall.c -- Syscall handler.
 *
 * When userspace executes `svc #0`, the CPU traps to EL1 and
 * vectors.S routes it here via trap_sync -> syscall_handler.
 *
 * Convention:
 *   x8  = syscall number
 *   x0-x5 = arguments
 *   x0  = return value (written back into the trap_frame)
 */

#include "syscall.h"
#include "sched.h"
#include "taskinfo.h"
#include "timer.h"
#include "uart.h"

/*
 * sys_write: write a string to the UART.
 *   x0 = pointer to string (in user VA space)
 *   x1 = length
 *
 * For simplicity, we trust the user pointer. A real OS would validate
 * that it points to mapped, user-accessible memory.
 */
static int64_t sys_write(struct trap_frame *frame) {
  const char *buf = (const char *)frame->regs[0];
  size_t len = frame->regs[1];

  for (size_t i = 0; i < len; i++) {
    if (buf[i] == '\n')
      uart_putc('\r');
    uart_putc(buf[i]);
  }

  return (int64_t)len;
}

void syscall_handler(struct trap_frame *frame) {
  uint64_t syscall_num = frame->regs[8]; /* x8 = syscall number */

  switch (syscall_num) {
  case SYS_WRITE:
    frame->regs[0] = (uint64_t)sys_write(frame);
    break;

  case SYS_EXIT:
    sched_exit_task(frame);
    /* Does not return -- schedule picks next task */
    break;

  case SYS_YIELD:
    schedule(frame);
    break;

  case SYS_SLEEP:
    /* x0 = number of ticks to sleep (~10ms each) */
    sched_sleep_task(frame, frame->regs[0]);
    break;

  case SYS_GETC:
    /* Returns the character read, or -1 if nothing available */
    frame->regs[0] = (uint64_t)uart_getc();
    break;

  case SYS_UPTIME:
    /* Returns number of timer ticks since boot */
    frame->regs[0] = timer_get_ticks();
    break;

  case SYS_PS:
    /* x0 = pointer to task_info array, x1= max entries */
    frame->regs[0] = (uint64_t)sched_get_tasks(
        (struct task_info *)frame->regs[0], (int)frame->regs[1]);
    break;

  default:
    uart_puts("[syscall] unknown syscall: ");
    uart_puthex(syscall_num);
    uart_puts("\n");
    frame->regs[0] = (uint64_t)-1;
    break;
  }
}
