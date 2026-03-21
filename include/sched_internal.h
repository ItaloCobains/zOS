#ifndef ZOS_SCHED_INTERNAL_H
#define ZOS_SCHED_INTERNAL_H

#include "sched.h"

/* Shared state between sched.c, fork.c, exec.c, fd.c */
extern struct task tasks[MAX_TASKS];
extern int current_task;
extern int num_tasks;

#endif
