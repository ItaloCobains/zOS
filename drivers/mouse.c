/*
 * mouse.c -- Mouse/tablet input via virtio-input.
 *
 * QEMU's virtio-tablet-device sends absolute coordinates (0-32767)
 * which we scale to framebuffer resolution.
 *
 * Virtio-input uses the same MMIO transport as virtio-blk (legacy).
 * Device ID = 18 for input devices.
 * Events come via a virtqueue: each event is 8 bytes.
 */

#include "types.h"
#include "mouse.h"
#include "fb.h"
#include "mm.h"
#include "uart.h"

/* Virtio MMIO registers (legacy) */
#define VIRTIO_MAGIC       0x000
#define VIRTIO_DEVICE_ID   0x008
#define VIRTIO_STATUS      0x070
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

#define VIRTIO_S_ACK       1
#define VIRTIO_S_DRIVER    2
#define VIRTIO_S_DRIVER_OK 4

#define VRING_DESC_F_WRITE 2
#define QUEUE_SIZE 64
#define PAGE_SZ    4096

/* Linux input event types */
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_ABS 0x03

/* Absolute axis codes */
#define ABS_X  0x00
#define ABS_Y  0x01

/* Button codes */
#define BTN_LEFT   0x110
#define BTN_RIGHT  0x111

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

/* Virtio input event (8 bytes) */
struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
};

static volatile uint32_t *base;
static struct vring_desc  *desc;
static struct vring_avail *avail;
static struct vring_used  *used;
static uint16_t last_used_idx;

/* Event buffers: one per descriptor */
static struct virtio_input_event events[QUEUE_SIZE] __attribute__((aligned(16)));

/* Mouse state */
static struct mouse_state state;

static uint32_t reg_read(uint32_t off)  { return base[off / 4]; }
static void reg_write(uint32_t off, uint32_t v) { base[off / 4] = v; }

void mouse_init(void)
{
    uint64_t mmio_base = 0x0A000000;

    /* Scan for virtio input device (ID=18) */
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *probe = (volatile uint32_t *)(mmio_base + i * 0x200);
        if (probe[0] != 0x74726976) continue;
        if (probe[VIRTIO_DEVICE_ID / 4] != 18) continue;
        base = probe;
        break;
    }

    if (!base) {
        uart_puts("[mouse] no virtio-input device found\n");
        return;
    }

    /* Init device */
    reg_write(VIRTIO_STATUS, 0);
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK);
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER);
    reg_write(VIRTIO_GUEST_FEAT, 0);
    reg_write(VIRTIO_GUEST_PGSZ, PAGE_SZ);

    /* Setup event queue (queue 0) */
    reg_write(VIRTIO_QUEUE_SEL, 0);
    uint32_t max_sz = reg_read(VIRTIO_QUEUE_MAX);
    uint32_t qsz = QUEUE_SIZE;
    if (qsz > max_sz) qsz = max_sz;

    reg_write(VIRTIO_QUEUE_NUM, qsz);
    reg_write(VIRTIO_QUEUE_ALIGN, PAGE_SZ);

    uint8_t *queue_mem = (uint8_t *)page_alloc();
    page_alloc(); /* second page for used ring */

    if (!queue_mem) {
        uart_puts("[mouse] failed to allocate queue\n");
        return;
    }

    desc  = (struct vring_desc *)queue_mem;
    avail = (struct vring_avail *)(queue_mem + qsz * 16);
    used  = (struct vring_used *)(queue_mem + PAGE_SZ);

    reg_write(VIRTIO_QUEUE_PFN, (uint32_t)((uint64_t)queue_mem / PAGE_SZ));

    /* Pre-fill available ring with event buffers (device writes events to us) */
    for (uint32_t i = 0; i < qsz; i++) {
        desc[i].addr  = (uint64_t)&events[i];
        desc[i].len   = sizeof(struct virtio_input_event);
        desc[i].flags = VRING_DESC_F_WRITE;
        desc[i].next  = 0;
        avail->ring[i] = (uint16_t)i;
    }
    avail->idx = qsz;

    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER | VIRTIO_S_DRIVER_OK);

    /* Notify device that buffers are available */
    __asm__ volatile("dmb ish" ::: "memory");
    reg_write(VIRTIO_QUEUE_NTFY, 0);

    state.x = FB_WIDTH / 2;
    state.y = FB_HEIGHT / 2;
    state.buttons = 0;
    state.clicked = 0;
    last_used_idx = 0;

    uart_puts("[mouse] virtio-input initialized\n");
}

void mouse_poll(void)
{
    if (!base) return;

    __asm__ volatile("dmb ish" ::: "memory");

    while (used->idx != last_used_idx) {
        uint32_t id = used->ring[last_used_idx % QUEUE_SIZE].id;
        struct virtio_input_event *ev = &events[id];

        if (ev->type == EV_ABS) {
            if (ev->code == ABS_X)
                state.x = (int)(ev->value * FB_WIDTH / 32768);
            else if (ev->code == ABS_Y)
                state.y = (int)(ev->value * FB_HEIGHT / 32768);
        } else if (ev->type == EV_KEY) {
            if (ev->code == BTN_LEFT) {
                if (ev->value) { state.buttons |= 1; state.clicked = 1; }
                else state.buttons &= ~1;
            } else if (ev->code == BTN_RIGHT) {
                if (ev->value) state.buttons |= 2;
                else state.buttons &= ~2;
            }
        }

        last_used_idx++;

        /* Re-queue the buffer */
        desc[id].addr  = (uint64_t)&events[id];
        desc[id].len   = sizeof(struct virtio_input_event);
        desc[id].flags = VRING_DESC_F_WRITE;

        avail->ring[avail->idx % QUEUE_SIZE] = (uint16_t)id;
        __asm__ volatile("dmb ish" ::: "memory");
        avail->idx++;
        __asm__ volatile("dmb ish" ::: "memory");
        reg_write(VIRTIO_QUEUE_NTFY, 0);
    }

    reg_write(VIRTIO_INT_ACK, reg_read(VIRTIO_INT_STATUS));
}

struct mouse_state mouse_get(void)
{
    mouse_poll();
    return state;
}
