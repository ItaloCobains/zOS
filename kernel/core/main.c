/*
 * main.c -- Kernel entry point.
 *
 * Initializes all subsystems and launches the shell.
 */

#include "types.h"
#include "uart.h"
#include "mm.h"
#include "mmu.h"
#include "gic.h"
#include "timer.h"
#include "sched.h"
#include "vfs.h"
#include "devfs.h"
#include "virtio_blk.h"
#include "ext2.h"

/* Linker symbols for embedded binaries */
extern char _bin_shell_start[], _bin_shell_end[];
extern char _bin_ls_start[],    _bin_ls_end[];
extern char _bin_cat_start[],   _bin_cat_end[];
extern char _bin_echo_start[],  _bin_echo_end[];
extern char _bin_hello_start[], _bin_hello_end[];
extern char _bin_ps_start[],    _bin_ps_end[];
extern char _bin_touch_start[], _bin_touch_end[];
extern char _bin_mkdir_start[], _bin_mkdir_end[];
extern char _bin_rm_start[],    _bin_rm_end[];

static void install_bin(const char *path, char *start, char *end)
{
    size_t size = (size_t)(end - start);
    int ino = vfs_open(path, 4);
    if (ino < 0) return;
    vfs_write(ino, start, size, 0);
}

static uint64_t *setup_shell(void)
{
    size_t size = (size_t)(_bin_shell_end - _bin_shell_start);
    size_t num_pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    uint64_t first_page = (uint64_t)page_alloc();
    if (!first_page) return NULL;

    for (size_t i = 1; i < num_pages; i++)
        page_alloc();

    uint8_t *src = (uint8_t *)_bin_shell_start;
    uint8_t *dst = (uint8_t *)first_page;
    for (size_t i = 0; i < size; i++)
        dst[i] = src[i];

    return mmu_create_user_tables(first_page, size);
}

void kmain(void)
{
    uart_init();
    uart_puts("\n============================\n");
    uart_puts("  zOS v0.2 -- aarch64\n");
    uart_puts("============================\n\n");

    mm_init();
    mmu_init();
    gic_init();
    timer_init();
    sched_init();

    /* Filesystem */
    vfs_init();
    vfs_mkdir("/tmp");
    vfs_mkdir("/bin");
    devfs_init();

    /* Block device + ext2 filesystem */
    virtio_blk_init();
    ext2_init();

    /* Install user binaries into /bin/ */
    install_bin("/bin/ls",    _bin_ls_start,    _bin_ls_end);
    install_bin("/bin/cat",   _bin_cat_start,   _bin_cat_end);
    install_bin("/bin/echo",  _bin_echo_start,  _bin_echo_end);
    install_bin("/bin/hello", _bin_hello_start, _bin_hello_end);
    install_bin("/bin/ps",    _bin_ps_start,    _bin_ps_end);
    install_bin("/bin/touch", _bin_touch_start, _bin_touch_end);
    install_bin("/bin/mkdir", _bin_mkdir_start, _bin_mkdir_end);
    install_bin("/bin/rm",    _bin_rm_start,    _bin_rm_end);
    uart_puts("[main] 8 binaries installed in /bin/\n");

    /* Set up FDs: stdin/stdout/stderr -> /dev/console */
    int console_ino = vfs_lookup("/dev/console");
    sched_init_fds(console_ino);

    /* Launch shell */
    uint64_t *shell_tables = setup_shell();
    if (!shell_tables) {
        uart_puts("[main] FATAL: could not set up shell\n");
        while (1) __asm__ volatile("wfe");
    }
    sched_create_task(0x00400000, shell_tables);

    __asm__ volatile("msr daifclr, #0xF");
    sched_start();

    while (1) __asm__ volatile("wfe");
}
