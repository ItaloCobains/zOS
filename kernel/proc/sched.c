/*
 * sched.c -- Round-robin preemptive scheduler.
 *
 * Manages task states and context switching. The tasks array is shared
 * with fork.c, exec.c, and fd.c via sched_internal.h.
 */

#include "sched.h"
#include "sched_internal.h"
#include "mm.h"
#include "mmu.h"
#include "taskinfo.h"
#include "uart.h"

struct task tasks[MAX_TASKS];
int current_task = -1;
int num_tasks = 0;

void sched_init(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        tasks[i].state = TASK_UNUSED;
        tasks[i].id = i;
    }
    uart_puts("[sched] scheduler initialized\n");
}

void sched_start(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_READY) {
            current_task = i;
            tasks[i].state = TASK_RUNNING;

            uint64_t ttbr = (uint64_t)tasks[i].ttbr0;
            __asm__ volatile(
                "msr ttbr0_el1, %0\n"
                "isb\n"
                "tlbi vmalle1is\n"
                "dsb ish\n"
                "ic iallu\n"
                "dsb ish\n"
                "isb\n"
                : : "r"(ttbr)
            );

            struct trap_frame boot_frame = tasks[i].frame;
            switch_to_user(&boot_frame);
        }
    }

    uart_puts("[sched] no tasks to start!\n");
    while (1) __asm__ volatile("wfe");
}

void sched_create_task(uint64_t entry_point, uint64_t *user_tables)
{
    if (num_tasks >= MAX_TASKS)
        return;

    int slot = -1;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return;

    struct task *t = &tasks[slot];
    t->state = TASK_READY;
    t->ttbr0 = user_tables;
    t->stack = (uint8_t *)page_alloc();
    if (!t->stack) {
        t->state = TASK_UNUSED;
        return;
    }

    for (int i = 0; i < 31; i++)
        t->frame.regs[i] = 0;
    t->frame.elr  = entry_point;
    t->frame.sp   = 0x00801000;
    t->frame.spsr = 0x00000000;

    num_tasks++;
}

void schedule(struct trap_frame *frame)
{
    if (current_task >= 0 && tasks[current_task].state == TASK_RUNNING) {
        tasks[current_task].frame = *frame;
        tasks[current_task].state = TASK_READY;
    }

    int prev = current_task;
    int next = (current_task + 1) % MAX_TASKS;
    for (int i = 0; i < MAX_TASKS; i++) {
        int idx = (next + i) % MAX_TASKS;
        if (tasks[idx].state == TASK_READY) {
            current_task = idx;
            tasks[idx].state = TASK_RUNNING;

            if (idx != prev) {
                uint64_t ttbr = (uint64_t)tasks[idx].ttbr0;
                __asm__ volatile(
                    "msr ttbr0_el1, %0\n"
                    "isb\n"
                    "tlbi vmalle1is\n"
                    "dsb ish\n"
                    "ic iallu\n"
                    "dsb ish\n"
                    "isb\n"
                    : : "r"(ttbr)
                );
                *frame = tasks[idx].frame;
            }
            return;
        }
    }

    uart_puts("[sched] no ready tasks, halting\n");
    while (1) __asm__ volatile("wfe");
}

void sched_exit_task(struct trap_frame *frame)
{
    if (current_task >= 0) {
        int parent = tasks[current_task].parent_id;
        if (parent >= 0 && tasks[parent].state == TASK_WAITING &&
            tasks[parent].wait_for == current_task) {
            tasks[parent].state = TASK_READY;
            tasks[parent].frame.regs[0] = (uint64_t)current_task;
        }

        mmu_free_user_tables(tasks[current_task].ttbr0);
        tasks[current_task].ttbr0 = NULL;
        tasks[current_task].state = TASK_UNUSED;
        num_tasks--;
    }
    schedule(frame);
}

void sched_sleep_task(struct trap_frame *frame, uint64_t ticks)
{
    if (current_task >= 0) {
        tasks[current_task].state = TASK_SLEEPING;
        tasks[current_task].sleep_ticks = ticks;
        tasks[current_task].frame = *frame;
    }
    schedule(frame);
}

void sched_tick(void)
{
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING) {
            if (tasks[i].sleep_ticks > 0)
                tasks[i].sleep_ticks--;
            if (tasks[i].sleep_ticks == 0)
                tasks[i].state = TASK_READY;
        }
    }
}

int sched_get_tasks(struct task_info *buf, int max)
{
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
