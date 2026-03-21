#ifndef ZOS_PIPE_H
#define ZOS_PIPE_H

#include "types.h"

/* Create a pipe. Returns two VFS inodes: read_ino and write_ino */
int pipe_create(int *read_ino, int *write_ino);

#endif
