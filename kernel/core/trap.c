/*
 * trap.c -- Exception dispatch.
 *
 * Called from vectors.S when an exception occurs. We identify
 * what happened and call the appropriate handler:
 *   - IRQ: check GIC, if timer -> reset timer + reschedule
 *   - Synchronous: check ESR_EL1, if SVC -> handle syscall
 *   - Data Abort from EL0: check if COW fault
 *   - Anything else: panic
 */

#include "types.h"
#include "trap.h"
#include "gic.h"
#include "timer.h"
#include "sched.h"
#include "syscall.h"
#include "mmu.h"
#include "gui.h"
#include "uart.h"

/* ESR_EL1 exception class (bits [31:26]) */
#define ESR_EC_SHIFT      26
#define ESR_EC_SVC64      0x15  /* SVC from AArch64 */
#define ESR_EC_DABORT_EL0 0x24  /* Data Abort from lower EL */

/* Timer IRQ number */
#define TIMER_IRQ 30

void trap_irq(struct trap_frame *frame)
{
    uint32_t irq = gic_acknowledge();

    if (irq == TIMER_IRQ) {
        timer_handler();
        sched_tick();
        gui_tick();
        gic_end_interrupt(irq);
        if ((frame->spsr & 0xF) == 0)
            schedule(frame);
    } else if (irq == 1023) {
        /* Spurious interrupt */
    } else {
        uart_puts("[trap] unknown IRQ: ");
        uart_puthex(irq);
        uart_puts("\n");
        gic_end_interrupt(irq);
    }
}

void trap_sync(struct trap_frame *frame)
{
    uint64_t esr;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));

    uint32_t ec = (esr >> ESR_EC_SHIFT) & 0x3F;

    if (ec == ESR_EC_SVC64) {
        syscall_handler(frame);
        return;
    }

    if (ec == ESR_EC_DABORT_EL0) {
        /* Data Abort from userspace -- check if it's a COW fault */
        uint64_t far;
        __asm__ volatile("mrs %0, far_el1" : "=r"(far));

        uint64_t *ttbr0;
        __asm__ volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));

        if (mmu_handle_cow(ttbr0, far))
            return;  /* COW resolved, re-execute the faulting instruction */

        /* Not a COW fault -- kill the process */
        uart_puts("[trap] segfault at ");
        uart_puthex(far);
        uart_puts(" pc=");
        uart_puthex(frame->elr);
        uart_puts("\n");
        sched_exit_task(frame);
        return;
    }

    uart_puts("[trap] PANIC: unexpected synchronous exception\n");
    uart_puts("  ESR_EL1 = ");
    uart_puthex(esr);
    uart_puts("\n  EC = ");
    uart_puthex(ec);
    uart_puts("\n  ELR_EL1 = ");
    uart_puthex(frame->elr);
    uart_puts("\n");
    while (1) __asm__ volatile("wfe");
}

void trap_error(struct trap_frame *frame)
{
    uart_puts("[trap] PANIC: unhandled exception\n");
    uart_puts("  ELR_EL1 = ");
    uart_puthex(frame->elr);
    uart_puts("\n  SPSR_EL1 = ");
    uart_puthex(frame->spsr);
    uart_puts("\n");
    while (1) __asm__ volatile("wfe");
}
