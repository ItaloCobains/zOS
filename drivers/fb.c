/*
 * fb.c -- Framebuffer driver using QEMU ramfb.
 *
 * ramfb is configured via fw_cfg: the guest allocates a framebuffer
 * in RAM and tells QEMU about it via the fw_cfg DMA interface.
 * QEMU then displays that memory region on the screen.
 *
 * fw_cfg MMIO on QEMU virt: 0x09020000
 */

#include "types.h"
#include "fb.h"
#include "mm.h"
#include "uart.h"
#include "font8x16.h"

/* fw_cfg MMIO registers */
#define FWCFG_BASE      0x09020000
#define FWCFG_DATA      (*(volatile uint8_t  *)(FWCFG_BASE + 0x00))
#define FWCFG_SEL       (*(volatile uint16_t *)(FWCFG_BASE + 0x08))
#define FWCFG_DMA       ((volatile uint64_t  *)(FWCFG_BASE + 0x10))

/* fw_cfg selectors */
#define FWCFG_FILE_DIR  0x0019

/* DMA control bits */
#define FWCFG_DMA_READ   0x02
#define FWCFG_DMA_WRITE  0x10
#define FWCFG_DMA_SELECT 0x08

/* Byte swap helpers (fw_cfg uses big-endian) */
static uint16_t bswap16(uint16_t v) {
    return (v >> 8) | (v << 8);
}

static uint32_t bswap32(uint32_t v) {
    return ((v >> 24) & 0xFF) | ((v >> 8) & 0xFF00) |
           ((v << 8) & 0xFF0000) | ((v << 24) & 0xFF000000);
}

static uint64_t bswap64(uint64_t v) {
    return ((uint64_t)bswap32((uint32_t)v) << 32) | bswap32((uint32_t)(v >> 32));
}

/* RAMFBCfg structure (all fields big-endian) */
struct __attribute__((packed)) ramfb_cfg {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

/* fw_cfg DMA access structure (all fields big-endian) */
struct __attribute__((packed)) fwcfg_dma {
    uint32_t control;
    uint32_t length;
    uint64_t address;
};

static uint32_t *framebuffer;

/*
 * Find a fw_cfg file by name. Returns the selector, or -1 if not found.
 */
static int fwcfg_find_file(const char *name)
{
    /* Select the file directory */
    FWCFG_SEL = bswap16(FWCFG_FILE_DIR);

    /* Read number of files (big-endian uint32) */
    uint32_t count = 0;
    for (int i = 0; i < 4; i++)
        count = (count << 8) | FWCFG_DATA;

    /* Iterate over files */
    for (uint32_t i = 0; i < count; i++) {
        /* Read file entry: size(4) + select(2) + reserved(2) + name(56) */
        uint32_t size = 0;
        for (int j = 0; j < 4; j++) size = (size << 8) | FWCFG_DATA;
        uint16_t sel = 0;
        for (int j = 0; j < 2; j++) sel = (sel << 8) | FWCFG_DATA;
        uint16_t res = 0;
        for (int j = 0; j < 2; j++) res = (res << 8) | FWCFG_DATA;
        (void)size; (void)res;

        char fname[56];
        for (int j = 0; j < 56; j++) fname[j] = FWCFG_DATA;

        /* Compare name */
        int match = 1;
        for (int j = 0; name[j]; j++) {
            if (name[j] != fname[j]) { match = 0; break; }
        }
        if (match) return (int)sel;
    }

    return -1;
}

/*
 * Write data to a fw_cfg file via DMA.
 */
static void fwcfg_dma_write(uint16_t sel, void *data, uint32_t len)
{
    struct fwcfg_dma dma;
    dma.control = bswap32(FWCFG_DMA_WRITE | FWCFG_DMA_SELECT | ((uint32_t)sel << 16));
    dma.length  = bswap32(len);
    dma.address = bswap64((uint64_t)data);

    uint64_t dma_addr = (uint64_t)&dma;

    /* Write DMA address (big-endian 64-bit) to the DMA register */
    *FWCFG_DMA = bswap64(dma_addr);

    /* Wait for completion (control field set to 0 by QEMU) */
    while (dma.control != 0)
        __asm__ volatile("dmb ish" ::: "memory");
}

void fb_init(void)
{
    /* Find ramfb file in fw_cfg */
    int sel = fwcfg_find_file("etc/ramfb");
    if (sel < 0) {
        uart_puts("[fb] ramfb not found (add -device ramfb to QEMU)\n");
        return;
    }

    /* Allocate framebuffer: 1024*768*4 = 3145728 bytes = 768 pages */
    uint32_t num_pages = FB_SIZE / PAGE_SIZE;
    framebuffer = (uint32_t *)page_alloc();
    if (!framebuffer) {
        uart_puts("[fb] failed to allocate framebuffer\n");
        return;
    }

    /* Allocate remaining contiguous pages */
    for (uint32_t i = 1; i < num_pages; i++)
        page_alloc();

    /* Fill with dark gray */
    for (uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; i++)
        framebuffer[i] = COLOR_DARKGRAY;

    /* Configure ramfb */
    struct ramfb_cfg cfg;
    cfg.addr   = bswap64((uint64_t)framebuffer);
    cfg.fourcc = bswap32(0x34325258);  /* "XR24" = XRGB8888 */
    cfg.flags  = bswap32(0);
    cfg.width  = bswap32(FB_WIDTH);
    cfg.height = bswap32(FB_HEIGHT);
    cfg.stride = bswap32(FB_STRIDE);

    fwcfg_dma_write((uint16_t)sel, &cfg, sizeof(cfg));

    uart_puts("[fb] framebuffer initialized (1024x768x32)\n");
}

void fb_pixel(int x, int y, uint32_t color)
{
    if (!framebuffer) return;
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) return;
    framebuffer[y * FB_WIDTH + x] = color;
}

void fb_rect(int x, int y, int w, int h, uint32_t color)
{
    if (!framebuffer) return;
    for (int dy = 0; dy < h; dy++) {
        int py = y + dy;
        if (py < 0 || py >= FB_HEIGHT) continue;
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            if (px < 0 || px >= FB_WIDTH) continue;
            framebuffer[py * FB_WIDTH + px] = color;
        }
    }
}

void fb_fill(uint32_t color)
{
    if (!framebuffer) return;
    for (uint32_t i = 0; i < FB_WIDTH * FB_HEIGHT; i++)
        framebuffer[i] = color;
}

void fb_char(int x, int y, char c, uint32_t fg, uint32_t bg)
{
    if (!framebuffer) return;
    if (c < FONT_FIRST || c > FONT_LAST) c = ' ';

    const unsigned char *glyph = font8x16[c - FONT_FIRST];

    for (int row = 0; row < FONT_HEIGHT; row++) {
        for (int col = 0; col < FONT_WIDTH; col++) {
            uint32_t color = (glyph[row] & (0x80 >> col)) ? fg : bg;
            fb_pixel(x + col, y + row, color);
        }
    }
}

void fb_text(int x, int y, const char *s, uint32_t fg, uint32_t bg)
{
    while (*s) {
        fb_char(x, y, *s, fg, bg);
        x += FONT_WIDTH;
        s++;
    }
}

uint32_t *fb_get_buffer(void)
{
    return framebuffer;
}
