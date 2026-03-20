/*
 * devfs.c -- Device filesystem.
 * Maps /dev/console to the UART driver.
 */

#include "types.h"
#include "vfs.h"
#include "uart.h"

static int console_read(int inode, void *buf, size_t len, size_t offset)
{
    (void)inode;
    (void)offset;

    uint8_t *dst = (uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        int c = uart_getc();
        if (c < 0)
            return (int)i;  /* return what we got so far */
        dst[i] = (uint8_t)c;
    }
    return (int)len;
}

static int console_write(int inode, const void *buf, size_t len, size_t offset)
{
    (void)inode;
    (void)offset;

    const uint8_t *src = (const uint8_t *)buf;
    for (size_t i = 0; i < len; i++) {
        if (src[i] == '\n')
            uart_putc('\r');
        uart_putc(src[i]);
    }
    return (int)len;
}

static struct file_ops console_ops = {
    .read  = console_read,
    .write = console_write,
};

void devfs_init(void)
{
    vfs_mkdir("/dev");
    vfs_register_device("/dev/console", &console_ops);
    uart_puts("[devfs] /dev/console registered\n");
}
