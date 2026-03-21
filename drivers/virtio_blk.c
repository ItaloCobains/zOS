/*
 * virtio_blk.c -- Virtio block device driver (MMIO legacy transport).
 *
 * QEMU virt machine uses virtio MMIO version 1 (legacy).
 * In legacy mode, the entire virtqueue (desc + avail + used) lives
 * in a contiguous region, and we give the device a single page
 * frame number (QueuePFN).
 */

#include "types.h"
#include "virtio_blk.h"
#include "uart.h"
#include "mm.h"

/* MMIO register offsets (legacy, version 1) */
#define VIRTIO_MAGIC       0x000
#define VIRTIO_VERSION     0x004
#define VIRTIO_DEVICE_ID   0x008
#define VIRTIO_HOST_FEAT   0x010
#define VIRTIO_GUEST_FEAT  0x020
#define VIRTIO_GUEST_PGSZ  0x028
#define VIRTIO_QUEUE_SEL   0x030
#define VIRTIO_QUEUE_MAX   0x034
#define VIRTIO_QUEUE_NUM   0x038
#define VIRTIO_QUEUE_ALIGN 0x03C
#define VIRTIO_QUEUE_PFN   0x040
#define VIRTIO_QUEUE_NTFY  0x050
#define VIRTIO_INT_STATUS  0x060
#define VIRTIO_INT_ACK     0x064
#define VIRTIO_STATUS      0x070
#define VIRTIO_CONFIG      0x100

#define VIRTIO_S_ACK       1
#define VIRTIO_S_DRIVER    2
#define VIRTIO_S_DRIVER_OK 4

#define VIRTIO_BLK_T_IN    0
#define VIRTIO_BLK_T_OUT   1

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

#define QUEUE_SIZE 16
#define PAGE_SZ    4096

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[QUEUE_SIZE];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
};

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[QUEUE_SIZE];
} __attribute__((packed));

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

static volatile uint32_t *base;
static struct vring_desc  *desc;
static struct vring_avail *avail;
static struct vring_used  *used;
static uint16_t last_used_idx;
static uint64_t disk_capacity;

/* Request buffers in kernel memory (not on stack, stable addresses for DMA) */
static struct virtio_blk_req req_header __attribute__((aligned(16)));
static uint8_t req_status __attribute__((aligned(16)));
static uint8_t dma_buf[512] __attribute__((aligned(512)));

static uint32_t reg_read(uint32_t off)  { return base[off / 4]; }
static void reg_write(uint32_t off, uint32_t v) { base[off / 4] = v; }

void virtio_blk_init(void)
{
    uint64_t mmio_base = 0x0A000000;

    for (int i = 0; i < 32; i++) {
        volatile uint32_t *probe = (volatile uint32_t *)(mmio_base + i * 0x200);
        if (probe[0] != 0x74726976) continue;
        if (probe[VIRTIO_DEVICE_ID / 4] != 2) continue;
        base = probe;
        break;
    }

    if (!base) {
        uart_puts("[virtio] no block device found\n");
        return;
    }

    /* Reset */
    reg_write(VIRTIO_STATUS, 0);
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK);
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER);

    /* Negotiate features (accept none) */
    reg_write(VIRTIO_GUEST_FEAT, 0);

    /* Set guest page size (legacy requirement) */
    reg_write(VIRTIO_GUEST_PGSZ, PAGE_SZ);

    /* Set up virtqueue 0 */
    reg_write(VIRTIO_QUEUE_SEL, 0);
    uint32_t max_size = reg_read(VIRTIO_QUEUE_MAX);
    if (max_size == 0) {
        uart_puts("[virtio] queue not available\n");
        return;
    }

    reg_write(VIRTIO_QUEUE_NUM, QUEUE_SIZE);
    reg_write(VIRTIO_QUEUE_ALIGN, PAGE_SZ);

    /*
     * Legacy layout: desc + avail in first page, used in second page.
     * Allocate 2 contiguous pages for the virtqueue.
     */
    uint8_t *queue_mem = (uint8_t *)page_alloc();
    page_alloc(); /* second page, must be contiguous */

    if (!queue_mem) {
        uart_puts("[virtio] failed to allocate queue\n");
        return;
    }

    desc  = (struct vring_desc *)queue_mem;
    avail = (struct vring_avail *)(queue_mem + QUEUE_SIZE * 16);
    used  = (struct vring_used *)(queue_mem + PAGE_SZ);

    /* Tell device where the queue is (page frame number) */
    reg_write(VIRTIO_QUEUE_PFN, (uint32_t)((uint64_t)queue_mem / PAGE_SZ));

    /* Driver is ready */
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER | VIRTIO_S_DRIVER_OK);

    /* Read capacity from device config */
    volatile uint32_t *config = (volatile uint32_t *)((uint64_t)base + VIRTIO_CONFIG);
    disk_capacity = config[0] | ((uint64_t)config[1] << 32);

    last_used_idx = 0;

    uart_puts("[virtio] disk: ");
    uart_puthex(disk_capacity * 512);
    uart_puts(" bytes\n");
}

static int virtio_blk_rw(uint32_t type, uint64_t sector, void *buf, uint32_t count)
{
    if (!base) return -1;

    for (uint32_t s = 0; s < count; s++) {
        req_header.type = type;
        req_header.reserved = 0;
        req_header.sector = sector + s;
        req_status = 0xFF;

        /* For writes, copy data to DMA buffer */
        if (type == VIRTIO_BLK_T_OUT) {
            uint8_t *src = (uint8_t *)buf + s * 512;
            for (int i = 0; i < 512; i++) dma_buf[i] = src[i];
        }

        /* Descriptor chain: header -> data -> status */
        desc[0].addr  = (uint64_t)&req_header;
        desc[0].len   = sizeof(struct virtio_blk_req);
        desc[0].flags = VRING_DESC_F_NEXT;
        desc[0].next  = 1;

        desc[1].addr  = (uint64_t)dma_buf;
        desc[1].len   = 512;
        desc[1].flags = VRING_DESC_F_NEXT |
                        (type == VIRTIO_BLK_T_IN ? VRING_DESC_F_WRITE : 0);
        desc[1].next  = 2;

        desc[2].addr  = (uint64_t)&req_status;
        desc[2].len   = 1;
        desc[2].flags = VRING_DESC_F_WRITE;
        desc[2].next  = 0;

        /* Memory barrier before updating avail ring */
        __asm__ volatile("dmb ish" ::: "memory");

        avail->ring[avail->idx % QUEUE_SIZE] = 0;
        __asm__ volatile("dmb ish" ::: "memory");
        avail->idx++;
        __asm__ volatile("dmb ish" ::: "memory");

        /* Notify device */
        reg_write(VIRTIO_QUEUE_NTFY, 0);

        /* Poll for completion */
        while (used->idx == last_used_idx)
            __asm__ volatile("dmb ish" ::: "memory");

        last_used_idx++;
        reg_write(VIRTIO_INT_ACK, reg_read(VIRTIO_INT_STATUS));

        if (req_status != 0) return -1;

        /* For reads, copy from DMA buffer */
        if (type == VIRTIO_BLK_T_IN) {
            uint8_t *dst = (uint8_t *)buf + s * 512;
            for (int i = 0; i < 512; i++) dst[i] = dma_buf[i];
        }
    }

    return 0;
}

int virtio_blk_read(uint64_t sector, void *buf, uint32_t count)
{
    return virtio_blk_rw(VIRTIO_BLK_T_IN, sector, buf, count);
}

int virtio_blk_write(uint64_t sector, const void *buf, uint32_t count)
{
    return virtio_blk_rw(VIRTIO_BLK_T_OUT, sector, (void *)buf, count);
}

uint64_t virtio_blk_capacity(void)
{
    return disk_capacity;
}
