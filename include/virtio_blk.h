#ifndef ZOS_VIRTIO_BLK_H
#define ZOS_VIRTIO_BLK_H

#include "types.h"

void virtio_blk_init(void);
int  virtio_blk_read(uint64_t sector, void *buf, uint32_t count);
int  virtio_blk_write(uint64_t sector, const void *buf, uint32_t count);
uint64_t virtio_blk_capacity(void);

#endif
