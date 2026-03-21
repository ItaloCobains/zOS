/*
 * net.c -- Networking: virtio-net driver + Ethernet + ARP + IP + ICMP.
 *
 * Minimal stack: just enough for ping to work.
 * QEMU user networking: gateway 10.0.2.2, guest 10.0.2.15.
 */

#include "net.h"
#include "mm.h"
#include "uart.h"

/* Virtio MMIO (legacy) */
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
#define VIRTIO_CONFIG      0x100

#define VIRTIO_S_ACK       1
#define VIRTIO_S_DRIVER    2
#define VIRTIO_S_DRIVER_OK 4
#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2
#define QUEUE_SIZE 16
#define PAGE_SZ    4096
#define PKT_SIZE   1526

struct vring_desc { uint64_t addr; uint32_t len; uint16_t flags; uint16_t next; } __attribute__((packed));
struct vring_avail { uint16_t flags; uint16_t idx; uint16_t ring[QUEUE_SIZE]; } __attribute__((packed));
struct vring_used_elem { uint32_t id; uint32_t len; };
struct vring_used { uint16_t flags; uint16_t idx; struct vring_used_elem ring[QUEUE_SIZE]; } __attribute__((packed));

/* Virtio-net header (prepended to every packet) */
struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    /* uint16_t num_buffers; -- only in modern, not legacy */
} __attribute__((packed));

/* Ethernet */
struct eth_hdr {
    uint8_t dst[6]; uint8_t src[6]; uint16_t type;
} __attribute__((packed));

/* ARP */
struct arp_pkt {
    uint16_t htype; uint16_t ptype; uint8_t hlen; uint8_t plen;
    uint16_t op;
    uint8_t sha[6]; uint8_t spa[4]; uint8_t tha[6]; uint8_t tpa[4];
} __attribute__((packed));

/* IP */
struct ip_hdr {
    uint8_t  vhl; uint8_t tos; uint16_t len;
    uint16_t id;  uint16_t off;
    uint8_t  ttl; uint8_t proto; uint16_t csum;
    uint32_t src; uint32_t dst;
} __attribute__((packed));

/* ICMP */
struct icmp_hdr {
    uint8_t type; uint8_t code; uint16_t csum;
    uint16_t id;  uint16_t seq;
} __attribute__((packed));

static volatile uint32_t *base;
/* TX queue */
static struct vring_desc  *tx_desc;
static volatile struct vring_avail *tx_avail;
static volatile struct vring_used  *tx_used;
static uint16_t tx_last_used;
/* RX queue */
static struct vring_desc  *rx_desc;
static volatile struct vring_avail *rx_avail;
static volatile struct vring_used  *rx_used;
static uint16_t rx_last_used;

static uint8_t tx_buf[PKT_SIZE] __attribute__((aligned(16)));
static uint8_t rx_bufs[QUEUE_SIZE][PKT_SIZE] __attribute__((aligned(16)));
static uint8_t our_mac[6];
static uint8_t gw_mac[6];
static int gw_mac_known;

/* Byte swap */
static uint16_t htons(uint16_t v) { return (v>>8)|(v<<8); }
static uint32_t htonl(uint32_t v) {
    return ((v>>24)&0xFF)|((v>>8)&0xFF00)|((v<<8)&0xFF0000)|((v<<24)&0xFF000000);
}

static uint32_t reg_read(uint32_t off)  { return base[off/4]; }
static void reg_write(uint32_t off, uint32_t v) { base[off/4] = v; }

static void setup_queue(int qidx, struct vring_desc **d, volatile struct vring_avail **a, volatile struct vring_used **u)
{
    reg_write(VIRTIO_QUEUE_SEL, qidx);
    uint32_t max_sz = reg_read(VIRTIO_QUEUE_MAX);
    uint32_t qsz = QUEUE_SIZE;
    if (qsz > max_sz) qsz = max_sz;
    reg_write(VIRTIO_QUEUE_NUM, qsz);
    reg_write(VIRTIO_QUEUE_ALIGN, PAGE_SZ);

    uint8_t *mem = (uint8_t *)page_alloc();
    page_alloc();
    *d = (struct vring_desc *)mem;
    *a = (volatile struct vring_avail *)(mem + qsz * 16);
    *u = (volatile struct vring_used *)(mem + PAGE_SZ);
    reg_write(VIRTIO_QUEUE_PFN, (uint32_t)((uint64_t)mem / PAGE_SZ));
}

void net_init(void)
{
    uint64_t mmio_base = 0x0A000000;
    for (int i = 31; i >= 0; i--) {
        volatile uint32_t *probe = (volatile uint32_t *)(mmio_base + i * 0x200);
        if (probe[0] != 0x74726976) continue;
        if (probe[VIRTIO_DEVICE_ID / 4] != 1) continue; /* net = ID 1 */
        base = probe;
        break;
    }
    if (!base) { uart_puts("[net] no virtio-net found\n"); return; }

    reg_write(VIRTIO_STATUS, 0);
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK);
    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER);
    reg_write(VIRTIO_GUEST_FEAT, 0);
    reg_write(VIRTIO_GUEST_PGSZ, PAGE_SZ);

    setup_queue(0, &rx_desc, &rx_avail, &rx_used); /* RX = queue 0 */
    setup_queue(1, &tx_desc, &tx_avail, &tx_used); /* TX = queue 1 */

    reg_write(VIRTIO_STATUS, VIRTIO_S_ACK | VIRTIO_S_DRIVER | VIRTIO_S_DRIVER_OK);

    /* Read MAC from config */
    volatile uint8_t *cfg = (volatile uint8_t *)((uint64_t)base + VIRTIO_CONFIG);
    for (int i = 0; i < 6; i++) our_mac[i] = cfg[i];

    /* Pre-fill RX queue with buffers */
    for (int i = 0; i < QUEUE_SIZE; i++) {
        rx_desc[i].addr = (uint64_t)&rx_bufs[i];
        rx_desc[i].len = PKT_SIZE;
        rx_desc[i].flags = VRING_DESC_F_WRITE;
        rx_desc[i].next = 0;
        rx_avail->ring[i] = (uint16_t)i;
    }
    rx_avail->idx = QUEUE_SIZE;
    __asm__ volatile("dmb sy" ::: "memory");
    reg_write(VIRTIO_QUEUE_NTFY, 0);

    rx_last_used = 0;
    tx_last_used = 0;
    gw_mac_known = 0;

    uart_puts("[net] MAC ");
    for (int i = 0; i < 6; i++) {
        uart_puthex(our_mac[i]);
        if (i < 5) uart_putc(':');
    }
    uart_puts("\n");
}

int net_send(const void *data, size_t len)
{
    if (!base || len > PKT_SIZE - sizeof(struct virtio_net_hdr)) return -1;

    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)tx_buf;
    for (int i = 0; i < (int)sizeof(*hdr); i++) ((uint8_t*)hdr)[i] = 0;
    uint8_t *pkt = tx_buf + sizeof(*hdr);
    const uint8_t *src = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) pkt[i] = src[i];

    tx_desc[0].addr = (uint64_t)tx_buf;
    tx_desc[0].len = sizeof(*hdr) + len;
    tx_desc[0].flags = 0;
    tx_desc[0].next = 0;

    tx_avail->ring[tx_avail->idx % QUEUE_SIZE] = 0;
    __asm__ volatile("dmb sy" ::: "memory");
    tx_avail->idx++;
    __asm__ volatile("dmb sy" ::: "memory");

    reg_write(VIRTIO_QUEUE_SEL, 1);
    reg_write(VIRTIO_QUEUE_NTFY, 1);

    /* Wait for TX completion */
    while (tx_used->idx == tx_last_used)
        __asm__ volatile("dmb sy" ::: "memory");
    tx_last_used++;

    reg_write(VIRTIO_INT_ACK, reg_read(VIRTIO_INT_STATUS));
    return 0;
}

int net_recv(void *buf, size_t max)
{
    if (!base) return -1;
    __asm__ volatile("dmb sy" ::: "memory");

    reg_write(VIRTIO_INT_ACK, reg_read(VIRTIO_INT_STATUS));

    if (rx_used->idx == rx_last_used) return 0;

    uint32_t id = rx_used->ring[rx_last_used % QUEUE_SIZE].id;
    uint32_t len = rx_used->ring[rx_last_used % QUEUE_SIZE].len;
    rx_last_used++;

    /* Skip virtio-net header */
    int hdr_sz = sizeof(struct virtio_net_hdr);
    int data_len = (int)len - hdr_sz;
    if (data_len <= 0 || (size_t)data_len > max) data_len = 0;

    uint8_t *src = rx_bufs[id] + hdr_sz;
    uint8_t *dst = (uint8_t *)buf;
    for (int i = 0; i < data_len; i++) dst[i] = src[i];

    /* Re-queue buffer */
    rx_desc[id].addr = (uint64_t)&rx_bufs[id];
    rx_desc[id].len = PKT_SIZE;
    rx_desc[id].flags = VRING_DESC_F_WRITE;
    rx_avail->ring[rx_avail->idx % QUEUE_SIZE] = (uint16_t)id;
    __asm__ volatile("dmb sy" ::: "memory");
    rx_avail->idx++;
    __asm__ volatile("dmb sy" ::: "memory");
    reg_write(VIRTIO_QUEUE_SEL, 0);
    reg_write(VIRTIO_QUEUE_NTFY, 0);

    return data_len;
}

/* Checksum helper */
static uint16_t checksum(const void *data, int len)
{
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void send_arp_request(uint32_t target_ip)
{
    uint8_t pkt[14 + 28]; /* eth + arp */
    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    struct arp_pkt *arp = (struct arp_pkt *)(pkt + 14);

    for (int i = 0; i < 6; i++) eth->dst[i] = 0xFF; /* broadcast */
    for (int i = 0; i < 6; i++) eth->src[i] = our_mac[i];
    eth->type = htons(0x0806); /* ARP */

    arp->htype = htons(1); arp->ptype = htons(0x0800);
    arp->hlen = 6; arp->plen = 4; arp->op = htons(1); /* request */
    for (int i = 0; i < 6; i++) arp->sha[i] = our_mac[i];
    uint32_t sip = htonl(OUR_IP);
    for (int i = 0; i < 4; i++) arp->spa[i] = ((uint8_t*)&sip)[i];
    for (int i = 0; i < 6; i++) arp->tha[i] = 0;
    uint32_t tip = htonl(target_ip);
    for (int i = 0; i < 4; i++) arp->tpa[i] = ((uint8_t*)&tip)[i];

    net_send(pkt, sizeof(pkt));
}

static void process_arp_reply(uint8_t *pkt)
{
    struct arp_pkt *arp = (struct arp_pkt *)(pkt + 14);
    if (htons(arp->op) == 2) { /* reply */
        for (int i = 0; i < 6; i++) gw_mac[i] = arp->sha[i];
        gw_mac_known = 1;
    }
}

static int ping_reply_received;
static uint16_t ping_seq;

static void process_icmp(uint8_t *pkt, int len)
{
    struct ip_hdr *ip = (struct ip_hdr *)(pkt + 14);
    if (ip->proto != 1) return; /* not ICMP */
    int ip_hdr_len = (ip->vhl & 0xF) * 4;
    struct icmp_hdr *icmp = (struct icmp_hdr *)(pkt + 14 + ip_hdr_len);
    (void)len;

    if (icmp->type == 0 && htons(icmp->seq) == ping_seq)
        ping_reply_received = 1;
}

static void send_ping(uint32_t dst_ip, uint16_t seq)
{
    uint8_t pkt[14 + 20 + 8 + 32]; /* eth + ip + icmp + data */
    int total = sizeof(pkt);

    struct eth_hdr *eth = (struct eth_hdr *)pkt;
    struct ip_hdr *ip = (struct ip_hdr *)(pkt + 14);
    struct icmp_hdr *icmp = (struct icmp_hdr *)(pkt + 14 + 20);

    for (int i = 0; i < 6; i++) eth->dst[i] = gw_mac[i];
    for (int i = 0; i < 6; i++) eth->src[i] = our_mac[i];
    eth->type = htons(0x0800);

    ip->vhl = 0x45; ip->tos = 0; ip->len = htons(20 + 8 + 32);
    ip->id = htons(1); ip->off = 0; ip->ttl = 64; ip->proto = 1;
    ip->csum = 0; ip->src = htonl(OUR_IP); ip->dst = htonl(dst_ip);
    ip->csum = checksum(ip, 20);

    icmp->type = 8; icmp->code = 0; icmp->csum = 0;
    icmp->id = htons(0x1234); icmp->seq = htons(seq);
    uint8_t *payload = pkt + 14 + 20 + 8;
    for (int i = 0; i < 32; i++) payload[i] = (uint8_t)i;
    icmp->csum = checksum(icmp, 8 + 32);

    net_send(pkt, total);
}

int net_ping(uint32_t ip, int count)
{
    if (!base) return -1;

    /* Resolve gateway MAC via ARP */
    if (!gw_mac_known) {
        send_arp_request(GATEWAY_IP);
        for (int tries = 0; tries < 1000000 && !gw_mac_known; tries++) {
            uint8_t buf[PKT_SIZE];
            int n = net_recv(buf, sizeof(buf));
            if (n > 14 && htons(((struct eth_hdr*)buf)->type) == 0x0806)
                process_arp_reply(buf);
        }
        if (!gw_mac_known) return -1;
    }

    int received = 0;
    for (int i = 0; i < count; i++) {
        ping_seq = (uint16_t)(i + 1);
        ping_reply_received = 0;
        send_ping(ip, ping_seq);

        for (int tries = 0; tries < 2000000 && !ping_reply_received; tries++) {
            uint8_t buf[PKT_SIZE];
            int n = net_recv(buf, sizeof(buf));
            if (n > 14 + 20) {
                uint16_t etype = htons(((struct eth_hdr*)buf)->type);
                if (etype == 0x0800) process_icmp(buf, n);
                else if (etype == 0x0806) process_arp_reply(buf);
            }
        }
        if (ping_reply_received) received++;
    }
    return received;
}
