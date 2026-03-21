/*
 * ext2.c -- Ext2 filesystem driver.
 *
 * Reads and writes a real ext2 filesystem on a virtio block device.
 * Supports: files (direct blocks only, max 12KB), directories, mkdir, unlink.
 * Block size: 1024 bytes. Single block group.
 */

#include "types.h"
#include "ext2.h"
#include "vfs.h"
#include "virtio_blk.h"
#include "uart.h"

/* Cached superblock and group descriptor */
static struct ext2_superblock sb;
static struct ext2_group_desc gd;
static uint8_t block_buf[EXT2_BLOCK_SIZE];

/* Read one ext2 block (1024 bytes = 2 sectors) */
static int read_block(uint32_t block, void *buf)
{
    return virtio_blk_read(block * EXT2_SECTORS_PER_BLOCK, buf,
                           EXT2_SECTORS_PER_BLOCK);
}

static int write_block(uint32_t block, const void *buf)
{
    return virtio_blk_write(block * EXT2_SECTORS_PER_BLOCK, buf,
                            EXT2_SECTORS_PER_BLOCK);
}

/* Read an inode from the inode table */
static int read_inode(uint32_t ino, struct ext2_inode *out)
{
    uint32_t inodes_per_block = EXT2_BLOCK_SIZE / 128;
    uint32_t block = gd.bg_inode_table + (ino - 1) / inodes_per_block;
    uint32_t offset = ((ino - 1) % inodes_per_block) * 128;

    if (read_block(block, block_buf) < 0) return -1;

    uint8_t *src = block_buf + offset;
    uint8_t *dst = (uint8_t *)out;
    for (int i = 0; i < 128; i++) dst[i] = src[i];

    return 0;
}

static int write_inode(uint32_t ino, struct ext2_inode *in)
{
    uint32_t inodes_per_block = EXT2_BLOCK_SIZE / 128;
    uint32_t block = gd.bg_inode_table + (ino - 1) / inodes_per_block;
    uint32_t offset = ((ino - 1) % inodes_per_block) * 128;

    if (read_block(block, block_buf) < 0) return -1;

    uint8_t *src = (uint8_t *)in;
    uint8_t *dst = block_buf + offset;
    for (int i = 0; i < 128; i++) dst[i] = src[i];

    return write_block(block, block_buf);
}

/* Allocate a free block using the block bitmap */
static int alloc_block(void)
{
    if (read_block(gd.bg_block_bitmap, block_buf) < 0) return -1;

    for (uint32_t i = 0; i < sb.s_blocks_count; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if (!(block_buf[byte] & (1 << bit))) {
            block_buf[byte] |= (1 << bit);
            write_block(gd.bg_block_bitmap, block_buf);
            sb.s_free_blocks_count--;
            gd.bg_free_blocks_count--;
            return (int)i;
        }
    }
    return -1;
}

static void free_block(uint32_t blk)
{
    if (read_block(gd.bg_block_bitmap, block_buf) < 0) return;
    block_buf[blk / 8] &= ~(1 << (blk % 8));
    write_block(gd.bg_block_bitmap, block_buf);
    sb.s_free_blocks_count++;
    gd.bg_free_blocks_count++;
}

/* Allocate a free inode using the inode bitmap */
static int alloc_inode(void)
{
    if (read_block(gd.bg_inode_bitmap, block_buf) < 0) return -1;

    for (uint32_t i = 0; i < sb.s_inodes_count; i++) {
        uint32_t byte = i / 8;
        uint32_t bit = i % 8;
        if (!(block_buf[byte] & (1 << bit))) {
            block_buf[byte] |= (1 << bit);
            write_block(gd.bg_inode_bitmap, block_buf);
            sb.s_free_inodes_count--;
            gd.bg_free_inodes_count--;
            return (int)(i + 1); /* inodes are 1-based */
        }
    }
    return -1;
}

/* Lookup a file in a directory by name. Returns inode number or -1. */
static int dir_lookup(uint32_t dir_ino, const char *name)
{
    struct ext2_inode dir;
    if (read_inode(dir_ino, &dir) < 0) return -1;

    for (int b = 0; b < 12 && dir.i_block[b]; b++) {
        if (read_block(dir.i_block[b], block_buf) < 0) return -1;

        uint32_t off = 0;
        while (off < EXT2_BLOCK_SIZE) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)(block_buf + off);
            if (de->inode == 0 || de->rec_len == 0) break;

            /* Compare name */
            int match = 1;
            int nlen = 0;
            while (name[nlen]) nlen++;
            if (nlen == de->name_len) {
                for (int i = 0; i < nlen; i++) {
                    if (name[i] != de->name[i]) { match = 0; break; }
                }
            } else {
                match = 0;
            }

            if (match) return (int)de->inode;

            off += de->rec_len;
        }
    }
    return -1;
}

int ext2_read_inode_pub(uint32_t ino, struct ext2_inode *out)
{
    return read_inode(ino, out);
}

/* Resolve a path like "subdir/file.txt" relative to a directory inode */
int ext2_path_lookup(const char *path)
{
    uint32_t cur = EXT2_ROOT_INO;

    while (*path == '/') path++;
    if (*path == 0) return (int)cur;

    while (*path) {
        char name[256];
        int len = 0;
        while (*path && *path != '/') name[len++] = *path++;
        name[len] = 0;
        while (*path == '/') path++;

        int ino = dir_lookup(cur, name);
        if (ino < 0) return -1;
        cur = (uint32_t)ino;
    }
    return (int)cur;
}

/* Add a directory entry to a directory */
static int dir_add_entry(uint32_t dir_ino, uint32_t child_ino,
                         const char *name, uint8_t file_type)
{
    struct ext2_inode dir;
    if (read_inode(dir_ino, &dir) < 0) return -1;

    int nlen = 0;
    while (name[nlen]) nlen++;

    uint16_t needed = (uint16_t)(8 + nlen);
    needed = (needed + 3) & ~3; /* align to 4 bytes */

    /* Try to find space in existing blocks */
    for (int b = 0; b < 12 && dir.i_block[b]; b++) {
        if (read_block(dir.i_block[b], block_buf) < 0) return -1;

        uint32_t off = 0;
        while (off < EXT2_BLOCK_SIZE) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)(block_buf + off);
            if (de->rec_len == 0) break;

            uint16_t actual = (uint16_t)(8 + de->name_len);
            actual = (actual + 3) & ~3;
            uint16_t space = de->rec_len - actual;

            if (space >= needed) {
                /* Split this entry */
                de->rec_len = actual;
                struct ext2_dir_entry *new_de =
                    (struct ext2_dir_entry *)(block_buf + off + actual);
                new_de->inode = child_ino;
                new_de->rec_len = space;
                new_de->name_len = (uint8_t)nlen;
                new_de->file_type = file_type;
                for (int i = 0; i < nlen; i++) new_de->name[i] = name[i];

                write_block(dir.i_block[b], block_buf);
                return 0;
            }
            off += de->rec_len;
        }
    }

    /* Need a new block for the directory */
    int new_blk = alloc_block();
    if (new_blk < 0) return -1;

    /* Find empty slot in i_block */
    int slot = -1;
    for (int b = 0; b < 12; b++) {
        if (dir.i_block[b] == 0) { slot = b; break; }
    }
    if (slot < 0) { free_block(new_blk); return -1; }

    /* Write new entry as only entry in the new block */
    for (int i = 0; i < EXT2_BLOCK_SIZE; i++) block_buf[i] = 0;
    struct ext2_dir_entry *de = (struct ext2_dir_entry *)block_buf;
    de->inode = child_ino;
    de->rec_len = EXT2_BLOCK_SIZE;
    de->name_len = (uint8_t)nlen;
    de->file_type = file_type;
    for (int i = 0; i < nlen; i++) de->name[i] = name[i];

    write_block(new_blk, block_buf);

    dir.i_block[slot] = new_blk;
    dir.i_size += EXT2_BLOCK_SIZE;
    dir.i_blocks += EXT2_SECTORS_PER_BLOCK;
    write_inode(dir_ino, &dir);

    return 0;
}

/* --- VFS file_ops callbacks --- */

static int ext2_vfs_read(int vfs_ino, void *buf, size_t len, size_t offset)
{
    /* The VFS inode stores the ext2 inode number in a way we need to recover.
     * For simplicity, we encode ext2_ino = vfs_ino (set during mount/open). */
    (void)vfs_ino;
    return -1; /* Placeholder -- actual reads go through ext2_read below */
}

static int ext2_vfs_write(int vfs_ino, const void *buf, size_t len, size_t offset)
{
    (void)vfs_ino;
    return -1;
}

static struct file_ops ext2_ops = {
    .read  = ext2_vfs_read,
    .write = ext2_vfs_write,
};

/*
 * Read from an ext2 file (by ext2 inode number).
 * Called by the VFS integration layer.
 */
int ext2_read_file(uint32_t ino, void *buf, size_t len, size_t offset)
{
    struct ext2_inode inode;
    if (read_inode(ino, &inode) < 0) return -1;

    if (offset >= inode.i_size) return 0;
    if (offset + len > inode.i_size) len = inode.i_size - offset;

    uint8_t *dst = (uint8_t *)buf;
    size_t done = 0;

    while (done < len) {
        uint32_t block_idx = (offset + done) / EXT2_BLOCK_SIZE;
        uint32_t block_off = (offset + done) % EXT2_BLOCK_SIZE;

        if (block_idx >= 12 || inode.i_block[block_idx] == 0) break;

        if (read_block(inode.i_block[block_idx], block_buf) < 0) break;

        size_t chunk = EXT2_BLOCK_SIZE - block_off;
        if (chunk > len - done) chunk = len - done;

        for (size_t i = 0; i < chunk; i++)
            dst[done + i] = block_buf[block_off + i];

        done += chunk;
    }

    return (int)done;
}

int ext2_write_file(uint32_t ino, const void *buf, size_t len, size_t offset)
{
    struct ext2_inode inode;
    if (read_inode(ino, &inode) < 0) return -1;

    const uint8_t *src = (const uint8_t *)buf;
    size_t done = 0;

    while (done < len) {
        uint32_t block_idx = (offset + done) / EXT2_BLOCK_SIZE;
        uint32_t block_off = (offset + done) % EXT2_BLOCK_SIZE;

        if (block_idx >= 12) break;

        /* Allocate block if needed */
        if (inode.i_block[block_idx] == 0) {
            int blk = alloc_block();
            if (blk < 0) break;
            inode.i_block[block_idx] = blk;
            inode.i_blocks += EXT2_SECTORS_PER_BLOCK;
            /* Zero new block */
            for (int i = 0; i < EXT2_BLOCK_SIZE; i++) block_buf[i] = 0;
            write_block(blk, block_buf);
        }

        if (read_block(inode.i_block[block_idx], block_buf) < 0) break;

        size_t chunk = EXT2_BLOCK_SIZE - block_off;
        if (chunk > len - done) chunk = len - done;

        for (size_t i = 0; i < chunk; i++)
            block_buf[block_off + i] = src[done + i];

        write_block(inode.i_block[block_idx], block_buf);
        done += chunk;
    }

    if (offset + done > inode.i_size)
        inode.i_size = offset + done;

    write_inode(ino, &inode);
    return (int)done;
}

/* Create a file in the ext2 filesystem */
int ext2_create(const char *path, uint16_t mode)
{
    /* Find parent directory */
    char name[256];
    int last_slash = -1;
    for (int i = 0; path[i]; i++)
        if (path[i] == '/') last_slash = i;

    uint32_t parent_ino;
    if (last_slash <= 0) {
        parent_ino = EXT2_ROOT_INO;
        int start = (path[0] == '/') ? 1 : 0;
        int i = 0;
        while (path[start + i]) { name[i] = path[start + i]; i++; }
        name[i] = 0;
    } else {
        char parent_path[256];
        for (int i = 0; i < last_slash; i++) parent_path[i] = path[i];
        parent_path[last_slash] = 0;
        int pino = ext2_path_lookup(parent_path);
        if (pino < 0) return -1;
        parent_ino = (uint32_t)pino;
        int i = 0;
        int start = last_slash + 1;
        while (path[start + i]) { name[i] = path[start + i]; i++; }
        name[i] = 0;
    }

    /* Check if already exists */
    if (dir_lookup(parent_ino, name) >= 0)
        return dir_lookup(parent_ino, name);

    /* Allocate new inode */
    int new_ino = alloc_inode();
    if (new_ino < 0) return -1;

    struct ext2_inode inode;
    for (int i = 0; i < 128; i++) ((uint8_t *)&inode)[i] = 0;
    inode.i_mode = mode;
    inode.i_links_count = 1;

    if (mode & EXT2_S_IFDIR) {
        /* Allocate a block for . and .. entries */
        int blk = alloc_block();
        if (blk < 0) return -1;

        for (int i = 0; i < EXT2_BLOCK_SIZE; i++) block_buf[i] = 0;

        /* "." entry */
        struct ext2_dir_entry *dot = (struct ext2_dir_entry *)block_buf;
        dot->inode = new_ino;
        dot->rec_len = 12;
        dot->name_len = 1;
        dot->file_type = EXT2_FT_DIR;
        dot->name[0] = '.';

        /* ".." entry */
        struct ext2_dir_entry *dotdot = (struct ext2_dir_entry *)(block_buf + 12);
        dotdot->inode = parent_ino;
        dotdot->rec_len = EXT2_BLOCK_SIZE - 12;
        dotdot->name_len = 2;
        dotdot->file_type = EXT2_FT_DIR;
        dotdot->name[0] = '.';
        dotdot->name[1] = '.';

        write_block(blk, block_buf);

        inode.i_block[0] = blk;
        inode.i_size = EXT2_BLOCK_SIZE;
        inode.i_blocks = EXT2_SECTORS_PER_BLOCK;
    }

    write_inode(new_ino, &inode);

    /* Add entry to parent directory */
    uint8_t ft = (mode & EXT2_S_IFDIR) ? EXT2_FT_DIR : EXT2_FT_REG_FILE;
    dir_add_entry(parent_ino, new_ino, name, ft);

    return new_ino;
}

/* Read directory entries */
int ext2_readdir(uint32_t dir_ino, struct dirent *entries, int max)
{
    struct ext2_inode dir;
    if (read_inode(dir_ino, &dir) < 0) return -1;

    int count = 0;
    for (int b = 0; b < 12 && dir.i_block[b] && count < max; b++) {
        if (read_block(dir.i_block[b], block_buf) < 0) break;

        uint32_t off = 0;
        while (off < EXT2_BLOCK_SIZE && count < max) {
            struct ext2_dir_entry *de = (struct ext2_dir_entry *)(block_buf + off);
            if (de->inode == 0 || de->rec_len == 0) break;

            /* Skip . and .. */
            if (!(de->name_len == 1 && de->name[0] == '.') &&
                !(de->name_len == 2 && de->name[0] == '.' && de->name[1] == '.')) {

                entries[count].inode = (int)de->inode;
                entries[count].type = (de->file_type == EXT2_FT_DIR) ? 2 : 1;
                for (int i = 0; i < de->name_len && i < 31; i++)
                    entries[count].name[i] = de->name[i];
                entries[count].name[de->name_len] = 0;
                count++;
            }
            off += de->rec_len;
        }
    }
    return count;
}

void ext2_init(void)
{
    uart_puts("[ext2] reading superblock...\n");
    /* Read superblock (at byte offset 1024 = sector 2) */
    uint8_t sb_buf[EXT2_BLOCK_SIZE];
    if (virtio_blk_read(2, sb_buf, EXT2_SECTORS_PER_BLOCK) < 0) {
        uart_puts("[ext2] failed to read superblock\n");
        return;
    }

    uint8_t *src = sb_buf;
    uint8_t *dst = (uint8_t *)&sb;
    for (int i = 0; i < (int)sizeof(sb); i++) dst[i] = src[i];

    if (sb.s_magic != EXT2_MAGIC) {
        uart_puts("[ext2] no ext2 filesystem found\n");
        return;
    }

    /* Read block group descriptor (block 2) */
    if (read_block(2, block_buf) < 0) {
        uart_puts("[ext2] failed to read group descriptor\n");
        return;
    }

    src = block_buf;
    dst = (uint8_t *)&gd;
    for (int i = 0; i < (int)sizeof(gd); i++) dst[i] = src[i];

    uart_puts("[ext2] mounted: ");
    uart_puthex(sb.s_blocks_count);
    uart_puts(" blocks, ");
    uart_puthex(sb.s_inodes_count);
    uart_puts(" inodes\n");

    /* Register /disk as mount point in VFS */
    vfs_mkdir("/disk");
}
