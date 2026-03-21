/*
 * gfx_console.c -- Graphical console device.
 *
 * Backs /dev/gtty: read returns keyboard input, write renders to
 * the terminal window via the window manager.
 */

#include "gfx_console.h"
#include "vfs.h"
#include "wm.h"
#include "keyboard.h"
#include "fb.h"
#include "uart.h"

static int terminal_win = -1;
static int device_inode = -1;

static int gtty_read(int inode, void *buf, size_t len, size_t offset)
{
    (void)inode;
    (void)offset;

    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        int c = keyboard_getc();
        if (c < 0)
            return (int)i;
        dst[i] = (uint8_t)c;
    }
    return (int)len;
}

static int gtty_write(int inode, const void *buf, size_t len, size_t offset)
{
    (void)inode;
    (void)offset;

    if (terminal_win < 0) return -1;

    const uint8_t *src = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        char c = (char)src[i];
        if (c == '\r') continue;  /* skip \r, only use \n */
        wm_putc(terminal_win, c);
    }
    return (int)len;
}

static struct file_ops gtty_ops = {
    .read  = gtty_read,
    .write = gtty_write,
};

void gfx_console_init(void)
{
    if (!fb_get_buffer()) return;

    /* Create terminal window */
    terminal_win = wm_create_window(50, 50, 600, 450, "Terminal");
    if (terminal_win < 0) {
        uart_puts("[gtty] failed to create terminal window\n");
        return;
    }

    /* Register /dev/gtty device */
    vfs_mkdir("/dev");  /* may already exist */
    device_inode = vfs_register_device("/dev/gtty", &gtty_ops);

    uart_puts("[gtty] graphical terminal ready\n");
}

int gfx_console_inode(void)
{
    return device_inode;
}
