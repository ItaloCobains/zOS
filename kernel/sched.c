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
#include "mmu.h"
#include "vfs.h"
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
    /* Wake parent if it's waiting for us */
    int parent = tasks[current_task].parent_id;
    if (parent >= 0 && tasks[parent].state == TASK_WAITING &&
        tasks[parent].wait_for == current_task) {
        tasks[parent].state = TASK_READY;
        tasks[parent].frame.regs[0] = (uint64_t)current_task; /* wait return value */
    }

    /* Free address space */
    mmu_free_user_tables(tasks[current_task].ttbr0);
    tasks[current_task].ttbr0 = NULL;

    tasks[current_task].state = TASK_UNUSED;
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

int sched_getpid(void)
{
    return current_task;
}

/*
 * Fork the current process. Creates a child with COW page tables.
 * Returns child pid to parent, 0 to child.
 */
int sched_fork(struct trap_frame *frame)
{
    /* Find a free task slot */
    int child_id = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            child_id = i;
            break;
        }
    }
    if (child_id < 0) return -1;

    struct task *parent = &tasks[current_task];
    struct task *child  = &tasks[child_id];

    /* Fork page tables with COW */
    uint64_t *child_tables = mmu_fork_tables(parent->ttbr0);
    if (!child_tables) return -1;

    /* Copy parent task state to child */
    child->id = child_id;
    child->state = TASK_READY;
    child->sleep_ticks = 0;
    child->parent_id = current_task;
    child->wait_for = -1;
    child->frame = *frame;
    child->stack = (uint8_t *)page_alloc();
    child->ttbr0 = child_tables;

    /* Copy FD table */
    for (int i = 0; i < MAX_FDS; i++)
        child->fds[i] = parent->fds[i];

    /* Child gets return value 0 */
    child->frame.regs[0] = 0;

    num_tasks++;

    /* Parent gets child pid */
    return child_id;
}

/*
 * Replace current process with a new binary from the filesystem.
 * `args` is copied to the top of the user stack so main() can read it.
 * The args string is placed at VA 0x00800F00 (near top of stack page).
 * x0 is set to point to it.
 */
int sched_exec(const char *path, const char *args, struct trap_frame *frame)
{
    /* Copy args to kernel buffer BEFORE freeing old address space */
    char args_buf[256];
    args_buf[0] = 0;
    if (args) {
        int i = 0;
        while (args[i] && i < 254) {
            args_buf[i] = args[i];
            i++;
        }
        args_buf[i] = 0;
    }

    /* Also copy path since it's in user memory */
    char path_buf[64];
    {
        int i = 0;
        while (path[i] && i < 62) {
            path_buf[i] = path[i];
            i++;
        }
        path_buf[i] = 0;
    }

    int ino = vfs_open(path_buf, 0);
    if (ino < 0) return -1;

    struct stat st;
    if (vfs_stat(path_buf, &st) < 0) return -1;

    size_t bin_size = st.size;
    if (bin_size == 0) return -1;

    size_t num_pages = (bin_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t first_page = (uint64_t)page_alloc();
    if (!first_page) return -1;

    for (size_t i = 1; i < num_pages; i++)
        page_alloc();

    vfs_read(ino, (void *)first_page, bin_size, 0);

    uint64_t *old_tables = tasks[current_task].ttbr0;
    uint64_t *new_tables = mmu_create_user_tables(first_page, bin_size);
    if (!new_tables) return -1;

    mmu_free_user_tables(old_tables);
    tasks[current_task].ttbr0 = new_tables;

    /* Switch to new address space */
    __asm__ volatile(
        "msr ttbr0_el1, %0\n"
        "isb\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "ic iallu\n"
        "dsb ish\n"
        "isb\n"
        : : "r"(new_tables)
    );

    /*
     * Copy args string to user stack page at VA 0x00800F00.
     * The stack page is physically mapped -- we need the physical
     * address to write to it from kernel space.
     * We walk the new page tables to find it.
     */
    uint64_t args_va = 0x00800F00;
    /* The stack page physical address: walk L1[0] -> L2[4] -> L3[0] */
    uint64_t *l2 = (uint64_t *)(new_tables[0] & 0x0000FFFFFFFFF000UL);
    uint64_t *l3 = (uint64_t *)(l2[4] & 0x0000FFFFFFFFF000UL);
    uint64_t stack_phys = l3[0] & 0x0000FFFFFFFFF000UL;
    char *args_dest = (char *)(stack_phys + 0xF00);  /* offset within page */

    {
        int i = 0;
        while (args_buf[i] && i < 254) {
            args_dest[i] = args_buf[i];
            i++;
        }
        args_dest[i] = 0;
    }

    /* Reset trap frame */
    for (int i = 0; i < 31; i++)
        frame->regs[i] = 0;
    frame->regs[0] = args_va;     /* x0 = pointer to args string */
    frame->elr  = 0x00400000;
    frame->sp   = 0x00800F00;     /* stack below the args */
    frame->spsr = 0x00000000;

    return 0;
}

/*
 * Wait for a child process to exit.
 * Blocks the current task until the child terminates.
 */
void sched_wait(int child_pid, struct trap_frame *frame)
{
    if (child_pid < 0 || child_pid >= MAX_TASKS) {
        frame->regs[0] = (uint64_t)-1;
        return;
    }

    /* If child already exited, return immediately */
    if (tasks[child_pid].state == TASK_DEAD ||
        tasks[child_pid].state == TASK_UNUSED) {
        frame->regs[0] = (uint64_t)child_pid;
        return;
    }

    /* Block until child exits */
    tasks[current_task].state = TASK_WAITING;
    tasks[current_task].wait_for = child_pid;
    tasks[current_task].frame = *frame;
    schedule(frame);
}
