/*
 * fd.c -- Per-process file descriptor table management.
 */

#include "sched.h"
#include "sched_internal.h"

struct fd_entry *sched_get_fd(int fd)
{
    if (current_task < 0 || fd < 0 || fd >= MAX_FDS)
        return NULL;
    struct fd_entry *e = &tasks[current_task].fds[fd];
    if (e->inode < 0)
        return NULL;
    return e;
}

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

void sched_free_fd(int fd)
{
    if (current_task < 0 || fd < 0 || fd >= MAX_FDS)
        return;
    tasks[current_task].fds[fd].inode = -1;
    tasks[current_task].fds[fd].offset = 0;
}

void sched_init_fds(int console_inode)
{
    for (int t = 0; t < MAX_TASKS; t++) {
        for (int f = 0; f < MAX_FDS; f++)
            tasks[t].fds[f].inode = -1;
        tasks[t].fds[0].inode = console_inode;
        tasks[t].fds[0].offset = 0;
        tasks[t].fds[1].inode = console_inode;
        tasks[t].fds[1].offset = 0;
        tasks[t].fds[2].inode = console_inode;
        tasks[t].fds[2].offset = 0;
    }
}
