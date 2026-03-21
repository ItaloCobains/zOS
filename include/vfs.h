#ifndef ZOS_VFS_H
#define ZOS_VFS_H

#include "types.h"

#define MAX_INODES 64
#define MAX_NAME 32
#define MAX_CHILDREN 16
#define FILE_DATA_MAX 4096

#define INODE_FREE 0
#define INODE_FILE 1
#define INODE_DIR 2
#define INODE_DEVICE 3
#define INODE_MOUNT  4

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREATE 4

struct stat {
  int type;
  uint64_t size;
};

struct dirent {
  int inode;
  int type;
  char name[MAX_NAME];
};

struct file_ops {
  int (*read)(int inode, void *buf, size_t len, size_t offset);
  int (*write)(int inode, const void *buf, size_t len, size_t offset);
};

struct inode {
  int type;
  char name[MAX_NAME];
  int parent;
  uint64_t size;
  uint8_t data[FILE_DATA_MAX];
  int children[MAX_CHILDREN];
  int num_children;
  struct file_ops *ops;
};

void vfs_init(void);
int vfs_open(const char *path, int flags);
int vfs_read(int inode, void *buf, size_t len, size_t offset);
int vfs_write(int inode, const void *buf, size_t len, size_t offset);
int vfs_stat(const char *path, struct stat *st);
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_readdir(int inode, struct dirent *entries, int max);

int vfs_lookup(const char *path);
int vfs_register_device(const char *path, struct file_ops *ops);
void vfs_set_ext2_available(void);

/* Ext2 inode encoding: VFS inode = EXT2_INO_BASE + ext2_ino */
#define EXT2_INO_BASE 10000

#endif
