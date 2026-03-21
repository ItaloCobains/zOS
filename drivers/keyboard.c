/*
 * keyboard.c -- Keyboard input via virtio-input.
 *
 * Scans for a second virtio-input device (the keyboard).
 * Converts Linux scancodes to ASCII.
 */

#include "keyboard.h"
#include "mouse.h"
#include "mm.h"
#include "uart.h"

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

#define EV_KEY 0x01

struct vring_desc {
    uint64_t addr; uint32_t len; uint16_t flags; uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags; uint16_t idx; uint16_t ring[QUEUE_SIZE];
} __attribute__((packed));

struct vring_used_elem { uint32_t id; uint32_t len; };
struct vring_used {
    uint16_t flags; uint16_t idx; struct vring_used_elem ring[QUEUE_SIZE];
} __attribute__((packed));

struct virtio_input_event {
    uint16_t type; uint16_t code; uint32_t value;
};

static volatile uint32_t *base;
static struct vring_desc  *desc;
static struct vring_avail *avail;
static struct vring_used  *used;
static uint16_t last_used_idx;
static struct virtio_input_event events[QUEUE_SIZE] __attribute__((aligned(16)));

/* Key buffer (ring) */
#define KEY_BUF_SIZE 64
static char key_buf[KEY_BUF_SIZE];
static int key_head, key_tail;

/* Scancode to ASCII (US QWERTY, lowercase only for simplicity) */
static const char scancode_to_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\r',
    0,  'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,  '\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,  ' ', 0,0,0,0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

static uint32_t reg_read(uint32_t off)  { return base[off / 4]; }
static void reg_write(uint32_t off, uint32_t v) { base[off / 4] = v; }

void keyboard_init(void)
{
    uint64_t mmio_base = 0x0A000000;
    int mouse_slot_id = mouse_get_mmio_slot();

    /* Find virtio-input device that is NOT the mouse */
    for (int i = 0; i < 32; i++) {
        if (i == mouse_slot_id) continue;  /* skip mouse device */
        volatile uint32_t *probe = (volatile uint32_t *)(mmio_base + i * 0x200);
        if (probe[0] != 0x74726976) continue;
        if (probe[VIRTIO_DEVICE_ID / 4] != 18) continue;
        base = probe;
        break;
    }

    if (!base) {
        uart_puts("[kbd] no virtio-keyboard found\n");
        return;
    }

    reg_write(VIRTIO_STATUS, 0);
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK);
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER);
    reg_write(VIRTIO_GUEST_FEAT, 0);
    reg_write(VIRTIO_GUEST_PGSZ, PAGE_SZ);

    reg_write(VIRTIO_QUEUE_SEL, 0);
    uint32_t max_sz = reg_read(VIRTIO_QUEUE_MAX);
    uint32_t qsz = QUEUE_SIZE;
    if (qsz > max_sz) qsz = max_sz;

    reg_write(VIRTIO_QUEUE_NUM, qsz);
    reg_write(VIRTIO_QUEUE_ALIGN, PAGE_SZ);

    uint8_t *queue_mem = (uint8_t *)page_alloc();
    page_alloc();

    desc  = (struct vring_desc *)queue_mem;
    avail = (struct vring_avail *)(queue_mem + qsz * 16);
    used  = (struct vring_used *)(queue_mem + PAGE_SZ);

    reg_write(VIRTIO_QUEUE_PFN, (uint32_t)((uint64_t)queue_mem / PAGE_SZ));

    for (uint32_t i = 0; i < qsz; i++) {
        desc[i].addr  = (uint64_t)&events[i];
        desc[i].len   = sizeof(struct virtio_input_event);
        desc[i].flags = VRING_DESC_F_WRITE;
        desc[i].next  = 0;
        avail->ring[i] = (uint16_t)i;
    }
    avail->idx = qsz;

    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER | VIRTIO_S_DRIVER_OK);
    __asm__ volatile("dmb ish" ::: "memory");
    reg_write(VIRTIO_QUEUE_NTFY, 0);

    key_head = key_tail = 0;
    last_used_idx = 0;

    uart_puts("[kbd] virtio-keyboard initialized\n");
}

void keyboard_poll(void)
{
    if (!base) return;
    __asm__ volatile("dmb ish" ::: "memory");

    while (used->idx != last_used_idx) {
        uint32_t id = used->ring[last_used_idx % QUEUE_SIZE].id;
        struct virtio_input_event *ev = &events[id];

        /* Key press (value=1), ignore release (value=0) and repeat (value=2) */
        if (ev->type == EV_KEY && ev->value == 1 && ev->code < 128) {
            char c = scancode_to_ascii[ev->code];
            if (c) {
                int next = (key_head + 1) % KEY_BUF_SIZE;
                if (next != key_tail) {
                    key_buf[key_head] = c;
                    key_head = next;
                }
            }
        }

        last_used_idx++;

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

int keyboard_getc(void)
{
    keyboard_poll();
    if (key_head == key_tail) return -1;
    char c = key_buf[key_tail];
    key_tail = (key_tail + 1) % KEY_BUF_SIZE;
    return c;
}
