/*
 * sched.c -- Round-robin preemptive scheduler.
 *
 * Maintains an array of tasks. On each timer interrupt (or yield),
 * saves the current task's registers from the trap_frame and picks
 * the next READY task in round-robin order.
 *
 * The first task switch is triggered by kmain calling schedule()
 * after creating tasks. Subsequent switches happen via timer IRQ.
 */

#include "sched.h"
#include "mm.h"
#include "taskinfo.h"
#include "uart.h"
static struct task tasks[MAX_TASKS];
static int current_task = -1; /* Index of currently running task */
static int num_tasks = 0;

void sched_init(void) {
  for (int i = 0; i < MAX_TASKS; i++) {
    tasks[i].state = TASK_UNUSED;
    tasks[i].id = i;
  }

  uart_puts("[sched] scheduler initialized\n");
}

/*
 * Start the scheduler. Picks the first READY task and jumps to it.
 * Does not return.
 */
void sched_start(void) {
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].state == TASK_READY) {
      current_task = i;
      tasks[i].state = TASK_RUNNING;

      /* Switch to this task's address space */
      uint64_t ttbr = (uint64_t)tasks[i].ttbr0;
      __asm__ volatile("msr ttbr0_el1, %0\n"
                       "isb\n"
                       "tlbi vmalle1is\n"
                       "dsb ish\n"
                       "ic iallu\n"
                       "dsb ish\n"
                       "isb\n"
                       :
                       : "r"(ttbr));

      /*
       * Copy frame to the boot stack, NOT the tasks array.
       * switch_to_user sets SP = &frame, so after eret SP_EL1
       * will be on the boot stack. If we passed &tasks[i].frame
       * directly, SP_EL1 would land inside the tasks array and
       * exception handlers would corrupt task data.
       */
      struct trap_frame boot_frame = tasks[i].frame;
      switch_to_user(&boot_frame);
    }
  }

  uart_puts("[sched] no tasks to start!\n");
  while (1)
    __asm__ volatile("wfe");
}

/*
 * Create a new task that will start executing at `entry_point` in EL0.
 * `user_tables` is the TTBR0 value for this task's address space.
 */
void sched_create_task(uint64_t entry_point, uint64_t *user_tables) {
  if (num_tasks >= MAX_TASKS) {
    uart_puts("[sched] ERROR: max tasks reached\n");
    return;
  }

  /* Find an unused slot */
  int slot = -1;
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].state == TASK_UNUSED) {
      slot = i;
      break;
    }
  }
  if (slot < 0)
    return;

  struct task *t = &tasks[slot];
  t->state = TASK_READY;
  t->ttbr0 = user_tables;

  /* Allocate a kernel stack for this task (used during exceptions) */
  t->stack = (uint8_t *)page_alloc();
  if (!t->stack) {
    uart_puts("[sched] ERROR: failed to allocate task stack\n");
    t->state = TASK_UNUSED;
    return;
  }

  /* Zero the trap frame */
  for (int i = 0; i < 31; i++)
    t->frame.regs[i] = 0;

  /* Set up initial trap_frame so switch_to_user lands in userspace */
  t->frame.elr = entry_point; /* Where to start executing */
  t->frame.sp = 0x00801000;   /* Top of user stack page (grows down) */
  t->frame.spsr = 0x00000000; /* EL0t: user mode, interrupts enabled */

  num_tasks++;

  uart_puts("[sched] created task ");
  uart_puthex(slot);
  uart_puts(" at entry ");
  uart_puthex(entry_point);
  uart_puts("\n");
}

/*
 * Pick the next task and switch to it.
 *
 * Called from:
 *   - trap_irq (timer interrupt): `frame` is the interrupted task's state
 *   - syscall_handler (sys_yield): `frame` is the calling task's state
 *
 * We save the current task's state into its task struct, then find
 * the next READY task, load its TTBR0, and call switch_to_user.
 */
void schedule(struct trap_frame *frame) {
  /* Save current task's state */
  if (current_task >= 0 && tasks[current_task].state == TASK_RUNNING) {
    tasks[current_task].frame = *frame;
    tasks[current_task].state = TASK_READY;
  }

  /* Find next ready task (round-robin) */
  int prev = current_task;
  int next = (current_task + 1) % MAX_TASKS;
  for (int i = 0; i < MAX_TASKS; i++) {
    int idx = (next + i) % MAX_TASKS;
    if (tasks[idx].state == TASK_READY) {
      current_task = idx;
      tasks[idx].state = TASK_RUNNING;

      /* Only switch address space if we're changing tasks */
      if (idx != prev) {
        uint64_t ttbr = (uint64_t)tasks[idx].ttbr0;
        __asm__ volatile("msr ttbr0_el1, %0\n"
                         "isb\n"
                         "tlbi vmalle1is\n"
                         "dsb ish\n"
                         "ic iallu\n"
                         "dsb ish\n"
                         "isb\n"
                         :
                         : "r"(ttbr));
        /* Load saved state when switching to a different task */
        *frame = tasks[idx].frame;
      }
      return;
    }
  }

  /* No ready tasks -- idle loop */
  uart_puts("[sched] no ready tasks, halting\n");
  while (1)
    __asm__ volatile("wfe");
}

/*
 * Kill the current task (called from sys_exit).
 */
void sched_exit_task(struct trap_frame *frame) {
  if (current_task >= 0) {
    uart_puts("[sched] task ");
    uart_puthex(current_task);
    uart_puts(" exited\n");
    tasks[current_task].state = TASK_DEAD;
    num_tasks--;
  }
  schedule(frame);
}

/*
 * Put the current task to sleep for `ticks` timer ticks (~10ms each).
 * The task goes to TASK_SLEEPING and won't be scheduled until the
 * counter reaches zero.
 */
void sched_sleep_task(struct trap_frame *frame, uint64_t ticks) {
  if (current_task >= 0) {
    tasks[current_task].state = TASK_SLEEPING;
    tasks[current_task].sleep_ticks = ticks;
    tasks[current_task].frame = *frame;
  }
  schedule(frame);
}

/*
 * Called every timer tick. Decrements sleep counters and wakes
 * tasks whose counter has reached zero.
 */
void sched_tick(void) {
  for (int i = 0; i < MAX_TASKS; i++) {
    if (tasks[i].state == TASK_SLEEPING) {
      if (tasks[i].sleep_ticks > 0)
        tasks[i].sleep_ticks--;
      if (tasks[i].sleep_ticks == 0)
        tasks[i].state = TASK_READY;
    }
  }
}

/* Get FD entry for current task. Returns NULL if invalid. */
struct fd_entry *sched_get_fd(int fd)
{
    if (current_task < 0 || fd < 0 || fd >= MAX_FDS)
        return NULL;
    struct fd_entry *e = &tasks[current_task].fds[fd];
    if (e->inode < 0)
        return NULL;
    return e;
}

/* Allocate a new FD for the current task. Returns fd number or -1. */
int sched_alloc_fd(int inode)
{
    if (current_task < 0)
        return -1;
    for (int i = 0; i < MAX_FDS; i++) {
        if (tasks[current_task].fds[i].inode < 0) {
            tasks[current_task].fds[i].inode = inode;
            tasks[current_task].fds[i].offset = 0;
            return i;
        }
    }
    return -1;
}

/* Free a FD for the current task. */
void sched_free_fd(int fd)
{
    if (current_task < 0 || fd < 0 || fd >= MAX_FDS)
        return;
    tasks[current_task].fds[fd].inode = -1;
    tasks[current_task].fds[fd].offset = 0;
}

/* Initialize FDs for all tasks: fd 0/1/2 -> console_inode */
void sched_init_fds(int console_inode)
{
    for (int t = 0; t < MAX_TASKS; t++) {
        for (int f = 0; f < MAX_FDS; f++)
            tasks[t].fds[f].inode = -1;
        /* stdin, stdout, stderr -> console */
        tasks[t].fds[0].inode = console_inode;
        tasks[t].fds[0].offset = 0;
        tasks[t].fds[1].inode = console_inode;
        tasks[t].fds[1].offset = 0;
        tasks[t].fds[2].inode = console_inode;
        tasks[t].fds[2].offset = 0;
    }
}

int sched_get_tasks(struct task_info *buf, int max) {
  int count = 0;
  for (int i = 0; i < MAX_TASKS && count < max; i++) {
    if (tasks[i].state != TASK_UNUSED) {
      buf[count].id = tasks[i].id;
      buf[count].state = tasks[i].state;
      buf[count].sleep_ticks = tasks[i].sleep_ticks;
      count++;
    }
  }
  return count;
}
