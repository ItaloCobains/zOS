#ifndef ZOS_SCHED_H
#define ZOS_SCHED_H

#include "taskinfo.h"
#include "trap.h"

#define MAX_FDS 16

#define MAX_TASKS 8
#define TASK_STACK_SIZE (4096 * 2) /* 8KB stack per task */

/* Possible task states */
enum task_state {
  TASK_UNUSED = 0,
  TASK_READY,
  TASK_RUNNING,
  TASK_SLEEPING,
  TASK_DEAD
};

struct fd_entry {
  int inode;
  size_t offset;
};

struct task {
  int id;
  enum task_state state;
  uint64_t sleep_ticks;    /* ticks remaining for TASK_SLEEPING */
  struct trap_frame frame; /* saved registers */
  uint8_t *stack;  /* base of kernel stack (for syscall/interrupt handling) */
  uint64_t *ttbr0; /* user page table base */
  struct fd_entry fds[MAX_FDS];
};

void sched_init(void);
void sched_start(void);
void sched_create_task(uint64_t entry_point, uint64_t *user_tables);
void schedule(struct trap_frame *frame);
void sched_exit_task(struct trap_frame *frame);
void sched_sleep_task(struct trap_frame *frame, uint64_t ticks);
void sched_tick(void);
int sched_get_tasks(struct task_info *buf, int max);

/* FD table access for current task */
struct fd_entry *sched_get_fd(int fd);
int sched_alloc_fd(int inode);
void sched_free_fd(int fd);
void sched_init_fds(int console_inode);

/* Defined in vectors.S */
extern void switch_to_user(struct trap_frame *frame);

#endif
