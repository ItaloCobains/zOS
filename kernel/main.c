/*
 * main.c -- Kernel entry point.
 *
 * Called from start.S after basic hardware init.
 * Initializes all subsystems in order and launches userspace.
 */

#include "types.h"
#include "uart.h"
#include "mm.h"
#include "mmu.h"
#include "gic.h"
#include "timer.h"
#include "sched.h"

/* Linker symbols for the embedded userspace binary */
extern char _user_start[];
extern char _user_end[];

/*
 * Copy the userspace binary to its own physical pages and create
 * page tables for it. Returns the TTBR0 value for the task.
 */
static uint64_t *setup_user_task(void)
{
    size_t user_size = (size_t)(_user_end - _user_start);

    uart_puts("[main] user binary: ");
    uart_puthex((uint64_t)_user_start);
    uart_puts(" - ");
    uart_puthex((uint64_t)_user_end);
    uart_puts(" (");
    uart_puthex(user_size);
    uart_puts(" bytes)\n");

    /*
     * Allocate physical pages and copy the user binary there.
     * This gives us a clean copy separate from the kernel image.
     */
    size_t num_pages = (user_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t first_page = (uint64_t)page_alloc();
    if (!first_page) {
        uart_puts("[main] ERROR: failed to allocate user pages\n");
        return NULL;
    }

    /* Allocate remaining pages (they must be contiguous for simplicity) */
    for (size_t i = 1; i < num_pages; i++) {
        void *p = page_alloc();
        /* We rely on the allocator giving contiguous pages since
         * we're allocating early when memory is empty */
        (void)p;
    }

    /* Copy user binary to allocated pages */
    uint8_t *src = (uint8_t *)_user_start;
    uint8_t *dst = (uint8_t *)first_page;
    for (size_t i = 0; i < user_size; i++)
        dst[i] = src[i];

    /* Create page tables mapping these pages at VA 0x00400000 */
    uint64_t *tables = mmu_create_user_tables(first_page, user_size);
    return tables;
}

/*
 * kmain -- kernel entry point, called from start.S.
 */
void kmain(void)
{
    /* 1. UART first -- we need output for debugging everything else */
    uart_init();
    uart_puts("\n============================\n");
    uart_puts("  zOS v0.1 -- aarch64\n");
    uart_puts("============================\n\n");

    /* 2. Physical memory allocator */
    mm_init();

    /* 3. MMU -- identity map kernel, enable virtual memory */
    mmu_init();

    /* 4. Interrupt controller */
    gic_init();

    /* 5. Timer for preemptive scheduling */
    timer_init();

    /* 6. Scheduler */
    sched_init();

    /* 7. Create userspace task */
    uint64_t *user_tables = setup_user_task();
    if (!user_tables) {
        uart_puts("[main] FATAL: could not set up user task\n");
        while (1) __asm__ volatile("wfe");
    }

    /* User entry point is at VA 0x00400000 (start of user text) */
    sched_create_task(0x00400000, user_tables);

    uart_puts("[main] starting first user task...\n\n");

    /*
     * Start the first task. We build a fake trap_frame and call
     * switch_to_user, which restores registers and does ERET to EL0.
     */
    struct trap_frame boot_frame;
    for (int i = 0; i < 31; i++)
        boot_frame.regs[i] = 0;
    boot_frame.elr  = 0x00400000;     /* User entry point */
    boot_frame.sp   = 0x00801000;     /* Top of user stack */
    boot_frame.spsr = 0x00000000;     /* EL0t mode */

    /* Switch address space to user task */
    __asm__ volatile(
        "msr ttbr0_el1, %0\n"
        "isb\n"
        "tlbi vmalle1is\n"
        "dsb ish\n"
        "ic iallu\n"
        "dsb ish\n"
        "isb\n"
        : : "r"(user_tables)
    );

    /* Enable interrupts and jump to userspace */
    __asm__ volatile("msr daifclr, #0xF");  /* Unmask all exceptions */
    switch_to_user(&boot_frame);

    /* Should never reach here */
    uart_puts("[main] ERROR: returned from userspace?!\n");
    while (1) __asm__ volatile("wfe");
}
