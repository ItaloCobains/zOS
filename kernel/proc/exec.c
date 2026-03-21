/*
 * exec.c -- Replace current process image with a new binary.
 */

#include "sched.h"
#include "sched_internal.h"
#include "mm.h"
#include "mmu.h"
#include "vfs.h"

int sched_exec(const char *path, const char *args, struct trap_frame *frame)
{
    /* Copy args and path to kernel buffers before freeing address space */
    char args_buf[256];
    args_buf[0] = 0;
    if (args) {
        int i = 0;
        while (args[i] && i < 254) { args_buf[i] = args[i]; i++; }
        args_buf[i] = 0;
    }

    char path_buf[64];
    {
        int i = 0;
        while (path[i] && i < 62) { path_buf[i] = path[i]; i++; }
        path_buf[i] = 0;
    }

    /* Read binary from filesystem */
    int ino = vfs_open(path_buf, 0);
    if (ino < 0) return -1;

    struct stat st;
    if (vfs_stat(path_buf, &st) < 0) return -1;

    size_t bin_size = st.size;
    if (bin_size == 0) return -1;

    size_t num_pages = (bin_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t first_page = (uint64_t)page_alloc();
    if (!first_page) return -1;

    for (size_t i = 1; i < num_pages; i++)
        page_alloc();

    vfs_read(ino, (void *)first_page, bin_size, 0);

    /* Replace address space */
    uint64_t *old_tables = tasks[current_task].ttbr0;
    uint64_t *new_tables = mmu_create_user_tables(first_page, bin_size);
    if (!new_tables) return -1;

    mmu_free_user_tables(old_tables);
    tasks[current_task].ttbr0 = new_tables;

    __asm__ volatile(
        "msr ttbr0_el1, %0\n"
        "isb\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "ic iallu\n"
        "dsb ish\n"
        "isb\n"
        : : "r"(new_tables)
    );

    /* Copy args to user stack (physical address of stack page) */
    uint64_t args_va = 0x00800F00;
    uint64_t *l2 = (uint64_t *)(new_tables[0] & 0x0000FFFFFFFFF000UL);
    uint64_t *l3 = (uint64_t *)(l2[4] & 0x0000FFFFFFFFF000UL);
    uint64_t stack_phys = l3[0] & 0x0000FFFFFFFFF000UL;
    char *args_dest = (char *)(stack_phys + 0xF00);

    {
        int i = 0;
        while (args_buf[i] && i < 254) { args_dest[i] = args_buf[i]; i++; }
        args_dest[i] = 0;
    }

    /* Reset trap frame to start fresh */
    for (int i = 0; i < 31; i++)
        frame->regs[i] = 0;
    frame->regs[0] = args_va;
    frame->elr  = 0x00400000;
    frame->sp   = 0x00800F00;
    frame->spsr = 0x00000000;

    return 0;
}
