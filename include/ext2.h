#ifndef ZOS_EXT2_H
#define ZOS_EXT2_H

#include "types.h"
#include "vfs.h"

#define EXT2_MAGIC        0xEF53
#define EXT2_BLOCK_SIZE   1024
#define EXT2_ROOT_INO     2
#define EXT2_SECTORS_PER_BLOCK (EXT2_BLOCK_SIZE / 512)

/* Inode types (in i_mode) */
#define EXT2_S_IFREG   0x8000
#define EXT2_S_IFDIR   0x4000

/* Directory entry file types */
#define EXT2_FT_REG_FILE 1
#define EXT2_FT_DIR      2

/* On-disk superblock (offset 1024 in the image) */
struct ext2_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    /* ... more fields we don't need */
};

/* On-disk block group descriptor */
struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
};

/* On-disk inode (128 bytes) */
struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
};

/* On-disk directory entry */
struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
};

void ext2_init(void);
int  ext2_path_lookup(const char *path);
int  ext2_create(const char *path, uint16_t mode);
int  ext2_read_file(uint32_t ino, void *buf, size_t len, size_t offset);
int  ext2_write_file(uint32_t ino, const void *buf, size_t len, size_t offset);
int  ext2_readdir(uint32_t dir_ino, struct dirent *entries, int max);
int  ext2_read_inode_pub(uint32_t ino, struct ext2_inode *out);

#endif
