/*
 * syscall.c -- Syscall handler.
 *
 * Convention:
 *   x8  = syscall number
 *   x0-x5 = arguments
 *   x0  = return value
 */

#include "types.h"
#include "syscall.h"
#include "sched.h"
#include "taskinfo.h"
#include "timer.h"
#include "uart.h"
#include "vfs.h"
#include "gui.h"

void syscall_handler(struct trap_frame *frame)
{
    uint64_t syscall_num = frame->regs[8];

    switch (syscall_num) {

    case SYS_WRITE: {
        /* write(fd, buf, len) -> bytes written */
        int fd = (int)frame->regs[0];
        const void *buf = (const void *)frame->regs[1];
        size_t len = frame->regs[2];
        struct fd_entry *e = sched_get_fd(fd);
        if (!e) { frame->regs[0] = (uint64_t)-1; break; }
        int n = vfs_write(e->inode, buf, len, e->offset);
        if (n > 0) e->offset += n;
        frame->regs[0] = (uint64_t)n;
        break;
    }

    case SYS_EXIT:
        sched_exit_task(frame);
        break;

    case SYS_YIELD:
        gui_tick();  /* update mouse/cursor during idle */
        schedule(frame);
        break;

    case SYS_SLEEP:
        sched_sleep_task(frame, frame->regs[0]);
        break;

    case SYS_GETC:
        frame->regs[0] = (uint64_t)uart_getc();
        break;

    case SYS_UPTIME:
        frame->regs[0] = timer_get_ticks();
        break;

    case SYS_PS:
        frame->regs[0] = (uint64_t)sched_get_tasks(
            (struct task_info *)frame->regs[0], (int)frame->regs[1]);
        break;

    case SYS_OPEN: {
        /* open(path, flags) -> fd */
        const char *path = (const char *)frame->regs[0];
        int flags = (int)frame->regs[1];
        int ino = vfs_open(path, flags);
        if (ino < 0) { frame->regs[0] = (uint64_t)-1; break; }
        int fd = sched_alloc_fd(ino);
        frame->regs[0] = (uint64_t)fd;
        break;
    }

    case SYS_READ: {
        /* read(fd, buf, len) -> bytes read */
        int fd = (int)frame->regs[0];
        void *buf = (void *)frame->regs[1];
        size_t len = frame->regs[2];
        struct fd_entry *e = sched_get_fd(fd);
        if (!e) { frame->regs[0] = (uint64_t)-1; break; }
        int n = vfs_read(e->inode, buf, len, e->offset);
        if (n > 0) e->offset += n;
        frame->regs[0] = (uint64_t)n;
        break;
    }

    case SYS_CLOSE: {
        /* close(fd) -> 0 */
        int fd = (int)frame->regs[0];
        sched_free_fd(fd);
        frame->regs[0] = 0;
        break;
    }

    case SYS_STAT: {
        /* stat(path, buf) -> 0 or -1 */
        const char *path = (const char *)frame->regs[0];
        struct stat *st = (struct stat *)frame->regs[1];
        frame->regs[0] = (uint64_t)vfs_stat(path, st);
        break;
    }

    case SYS_MKDIR: {
        /* mkdir(path) -> 0 or -1 */
        const char *path = (const char *)frame->regs[0];
        frame->regs[0] = (uint64_t)vfs_mkdir(path);
        break;
    }

    case SYS_READDIR: {
        /* readdir(fd, entries, max) -> count */
        int fd = (int)frame->regs[0];
        struct dirent *entries = (struct dirent *)frame->regs[1];
        int max = (int)frame->regs[2];
        struct fd_entry *e = sched_get_fd(fd);
        if (!e) { frame->regs[0] = (uint64_t)-1; break; }
        frame->regs[0] = (uint64_t)vfs_readdir(e->inode, entries, max);
        break;
    }

    case SYS_UNLINK: {
        const char *path = (const char *)frame->regs[0];
        frame->regs[0] = (uint64_t)vfs_unlink(path);
        break;
    }

    case SYS_FORK:
        frame->regs[0] = (uint64_t)sched_fork(frame);
        break;

    case SYS_EXEC: {
        /* exec(path, args) */
        const char *path = (const char *)frame->regs[0];
        const char *args = (const char *)frame->regs[1];
        int ret = sched_exec(path, args, frame);
        if (ret < 0)
            frame->regs[0] = (uint64_t)-1;
        break;
    }

    case SYS_WAIT:
        sched_wait((int)frame->regs[0], frame);
        break;

    case SYS_GETPID:
        frame->regs[0] = (uint64_t)sched_getpid();
        break;

    default:
        uart_puts("[syscall] unknown syscall: ");
        uart_puthex(syscall_num);
        uart_puts("\n");
        frame->regs[0] = (uint64_t)-1;
        break;
    }
}
