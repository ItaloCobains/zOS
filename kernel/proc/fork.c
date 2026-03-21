/*
 * fork.c -- Process duplication with COW.
 */

#include "sched.h"
#include "sched_internal.h"
#include "mm.h"
#include "mmu.h"

int sched_fork(struct trap_frame *frame)
{
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

    uint64_t *child_tables = mmu_fork_tables(parent->ttbr0);
    if (!child_tables) return -1;

    child->id = child_id;
    child->state = TASK_READY;
    child->sleep_ticks = 0;
    child->parent_id = current_task;
    child->wait_for = -1;
    child->frame = *frame;
    child->stack = (uint8_t *)page_alloc();
    child->ttbr0 = child_tables;

    for (int i = 0; i < MAX_FDS; i++)
        child->fds[i] = parent->fds[i];

    child->frame.regs[0] = 0;  /* child gets return value 0 */

    num_tasks++;
    return child_id;  /* parent gets child pid */
}

void sched_wait(int child_pid, struct trap_frame *frame)
{
    if (child_pid < 0 || child_pid >= MAX_TASKS) {
        frame->regs[0] = (uint64_t)-1;
        return;
    }

    if (tasks[child_pid].state == TASK_DEAD ||
        tasks[child_pid].state == TASK_UNUSED) {
        frame->regs[0] = (uint64_t)child_pid;
        return;
    }

    tasks[current_task].state = TASK_WAITING;
    tasks[current_task].wait_for = child_pid;
    tasks[current_task].frame = *frame;
    schedule(frame);
}
