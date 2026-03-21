/*
 * pipe.c -- In-memory pipe for IPC between processes.
 *
 * A pipe has a 4KB ring buffer. One end reads, the other writes.
 * Each pipe is registered as two VFS device inodes (read + write).
 */

#include "pipe.h"
#include "vfs.h"

#define MAX_PIPES 8
#define PIPE_BUF_SIZE 4096

struct pipe {
    int active;
    uint8_t buf[PIPE_BUF_SIZE];
    int head, tail;
    int read_ino, write_ino;
    int write_closed;
};

static struct pipe pipes[MAX_PIPES];

static struct pipe *find_pipe_by_ino(int ino)
{
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].active &&
            (pipes[i].read_ino == ino || pipes[i].write_ino == ino))
            return &pipes[i];
    }
    return NULL;
}

static int pipe_read(int ino, void *buf, size_t len, size_t offset)
{
    (void)offset;
    struct pipe *p = find_pipe_by_ino(ino);
    if (!p) return -1;

    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;

    while (done < len) {
        if (p->head == p->tail) {
            if (p->write_closed || done > 0) break;
            break;  /* no data available */
        }
        dst[done++] = p->buf[p->tail];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
    }
    return (int)done;
}

static int pipe_write(int ino, const void *buf, size_t len, size_t offset)
{
    (void)offset;
    struct pipe *p = find_pipe_by_ino(ino);
    if (!p) return -1;

    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;

    while (done < len) {
        int next = (p->head + 1) % PIPE_BUF_SIZE;
        if (next == p->tail) break;  /* pipe full */
        p->buf[p->head] = src[done++];
        p->head = next;
    }
    return (int)done;
}

static struct file_ops pipe_read_ops = { .read = pipe_read, .write = NULL };
static struct file_ops pipe_write_ops = { .read = NULL, .write = pipe_write };

int pipe_create(int *read_ino, int *write_ino)
{
    int slot = -1;
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].active) { slot = i; break; }
    }
    if (slot < 0) return -1;

    struct pipe *p = &pipes[slot];
    p->active = 1;
    p->head = 0;
    p->tail = 0;
    p->write_closed = 0;

    /* Register as anonymous devices */
    p->read_ino = vfs_register_device("/dev/pipe_r", &pipe_read_ops);
    p->write_ino = vfs_register_device("/dev/pipe_w", &pipe_write_ops);

    if (p->read_ino < 0 || p->write_ino < 0) {
        p->active = 0;
        return -1;
    }

    *read_ino = p->read_ino;
    *write_ino = p->write_ino;
    return 0;
}
