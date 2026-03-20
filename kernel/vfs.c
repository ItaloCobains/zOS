/*
 * vfs.c -- Virtual File System layer.
 * Routes file operations to the correct backend (ramfs, devfs).
 */

#include "vfs.h"
#include "types.h"
#include "uart.h"

static struct inode inodes[MAX_INODES];

static int alloc_inode(void) {
  for (int i = 0; i < MAX_INODES; i++) {
    if (inodes[i].type == INODE_FREE)
      return i;
  }
  return -1;
}

static int streq(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == 0 && *b == 0;
}

static void strcopy(char *dst, const char *src, int max) {
  int i = 0;
  while (i < max - 1 && src[i]) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = 0;
}

int vfs_lookup(const char *path) {
  if (path[0] != '/')
    return -1;

  int cur = 0;
  path++;

  if (*path == 0)
    return 0;

  while (*path) {
    char name[MAX_NAME];
    int len = 0;
    while (*path && *path != '/' && len < MAX_NAME - 1) {
      name[len++] = *path++;
    }
    name[len] = 0;

    if (*path == '/')
      path++;

    if (inodes[cur].type != INODE_DIR)
      return -1;

    int found = -1;

    for (int i = 0; i < inodes[cur].num_children; i++) {
      int child = inodes[cur].children[i];
      if (streq(inodes[child].name, name)) {
        found = child;
        break;
      }
    }

    if (found < 0)
      return -1;

    cur = found;
  }

  return cur;
}

static int lookup_parent(const char *path, char *name_out)
{
    if (path[0] != '/')
        return -1;

    int last_slash = 0;
    for (int i = 0; path[i]; i++) {
        if (path[i] == '/')
            last_slash = i;
    }

    if (last_slash == 0) {
        strcopy(name_out, path + 1, MAX_NAME);
        return 0;
    }

    char parent_path[128];
    for (int i = 0; i < last_slash && i < 127; i++)
        parent_path[i] = path[i];
    parent_path[last_slash] = 0;

    strcopy(name_out, path + last_slash + 1, MAX_NAME);
    return vfs_lookup(parent_path);
}

void vfs_init(void)
{
    for (int i = 0; i < MAX_INODES; i++)
        inodes[i].type = INODE_FREE;

    inodes[0].type = INODE_DIR;
    strcopy(inodes[0].name, "/", MAX_NAME);
    inodes[0].parent = 0;
    inodes[0].num_children = 0;

    uart_puts("[vfs] initialized\n");
}

int vfs_open(const char *path, int flags)
{
    int ino = vfs_lookup(path);

    if (ino >= 0)
        return ino;

    if (!(flags & O_CREATE))
        return -1;

    char name[MAX_NAME];
    int parent = lookup_parent(path, name);
    if (parent < 0 || inodes[parent].type != INODE_DIR)
        return -1;

    if (inodes[parent].num_children >= MAX_CHILDREN)
        return -1;

    int new_ino = alloc_inode();
    if (new_ino < 0)
        return -1;

    inodes[new_ino].type = INODE_FILE;
    strcopy(inodes[new_ino].name, name, MAX_NAME);
    inodes[new_ino].parent = parent;
    inodes[new_ino].size = 0;

    inodes[parent].children[inodes[parent].num_children++] = new_ino;

    return new_ino;
}

int vfs_read(int ino, void *buf, size_t len, size_t offset)
{
    if (ino < 0 || ino >= MAX_INODES)
        return -1;

    struct inode *node = &inodes[ino];

    if (node->type == INODE_DEVICE && node->ops && node->ops->read)
        return node->ops->read(ino, buf, len, offset);

    if (node->type != INODE_FILE)
        return -1;

    if (offset >= node->size)
        return 0;

    size_t avail = node->size - offset;
    if (len > avail)
        len = avail;

    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++)
        dst[i] = node->data[offset + i];

    return (int)len;
}

int vfs_write(int ino, const void *buf, size_t len, size_t offset)
{
    if (ino < 0 || ino >= MAX_INODES)
        return -1;

    struct inode *node = &inodes[ino];

    if (node->type == INODE_DEVICE && node->ops && node->ops->write)
        return node->ops->write(ino, buf, len, offset);

    if (node->type != INODE_FILE)
        return -1;

    if (offset + len > FILE_DATA_MAX)
        len = FILE_DATA_MAX - offset;

    const uint8_t *src = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++)
        node->data[offset + i] = src[i];

    if (offset + len > node->size)
        node->size = offset + len;

    return (int)len;
}

int vfs_stat(const char *path, struct stat *st)
{
    int ino = vfs_lookup(path);
    if (ino < 0)
        return -1;

    st->type = inodes[ino].type;
    st->size = inodes[ino].size;
    return 0;
}

int vfs_mkdir(const char *path)
{
    if (vfs_lookup(path) >= 0)
        return -1;

    char name[MAX_NAME];
    int parent = lookup_parent(path, name);
    if (parent < 0 || inodes[parent].type != INODE_DIR)
        return -1;

    if (inodes[parent].num_children >= MAX_CHILDREN)
        return -1;

    int ino = alloc_inode();
    if (ino < 0)
        return -1;

    inodes[ino].type = INODE_DIR;
    strcopy(inodes[ino].name, name, MAX_NAME);
    inodes[ino].parent = parent;
    inodes[ino].num_children = 0;

    inodes[parent].children[inodes[parent].num_children++] = ino;

    return 0;
}

int vfs_unlink(const char *path)
{
    int ino = vfs_lookup(path);
    if (ino <= 0)
        return -1;

    if (inodes[ino].type == INODE_DIR)
        return -1;

    int parent = inodes[ino].parent;

    for (int i = 0; i < inodes[parent].num_children; i++) {
        if (inodes[parent].children[i] == ino) {
            for (int j = i; j < inodes[parent].num_children - 1; j++)
                inodes[parent].children[j] = inodes[parent].children[j + 1];
            inodes[parent].num_children--;
            break;
        }
    }

    inodes[ino].type = INODE_FREE;
    return 0;
}

int vfs_readdir(int ino, struct dirent *entries, int max)
{
    if (ino < 0 || ino >= MAX_INODES)
        return -1;

    if (inodes[ino].type != INODE_DIR)
        return -1;

    int count = 0;
    for (int i = 0; i < inodes[ino].num_children && count < max; i++) {
        int child = inodes[ino].children[i];
        entries[count].inode = child;
        entries[count].type = inodes[child].type;
        strcopy(entries[count].name, inodes[child].name, MAX_NAME);
        count++;
    }

    return count;
}

int vfs_register_device(const char *path, struct file_ops *ops)
{
    char name[MAX_NAME];
    int parent = lookup_parent(path, name);
    if (parent < 0)
        return -1;

    int ino = alloc_inode();
    if (ino < 0)
        return -1;

    inodes[ino].type = INODE_DEVICE;
    strcopy(inodes[ino].name, name, MAX_NAME);
    inodes[ino].parent = parent;
    inodes[ino].ops = ops;

    inodes[parent].children[inodes[parent].num_children++] = ino;

    return ino;
}
