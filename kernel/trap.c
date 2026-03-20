/*
 * trap.c -- Exception dispatch.
 *
 * Called from vectors.S when an exception occurs. We identify
 * what happened and call the appropriate handler:
 *   - IRQ: check GIC, if timer -> reset timer + reschedule
 *   - Synchronous: check ESR_EL1, if SVC -> handle syscall
 *   - Anything else: panic
 */

#include "types.h"
#include "trap.h"
#include "gic.h"
#include "timer.h"
#include "sched.h"
#include "syscall.h"
#include "uart.h"

/* ESR_EL1 exception class (bits [31:26]) */
#define ESR_EC_SHIFT 26
#define ESR_EC_SVC64 0x15  /* SVC from AArch64 */

/* Timer IRQ number */
#define TIMER_IRQ 30

/*
 * IRQ handler: called when an interrupt arrives (from EL0 or EL1).
 */
void trap_irq(struct trap_frame *frame)
{
    uint32_t irq = gic_acknowledge();

    if (irq == TIMER_IRQ) {
        timer_handler();
        sched_tick();
        gic_end_interrupt(irq);
        /* Only reschedule if IRQ interrupted userspace (EL0) */
        if ((frame->spsr & 0xF) == 0)
            schedule(frame);
    } else if (irq == 1023) {
        /* Spurious interrupt -- ignore */
    } else {
        uart_puts("[trap] unknown IRQ: ");
        uart_puthex(irq);
        uart_puts("\n");
        gic_end_interrupt(irq);
    }
}

/*
 * Synchronous exception handler.
 *
 * Read ESR_EL1 to find out what happened:
 *   - EC = 0x15: SVC instruction from AArch64 (syscall)
 *   - Anything else: unexpected, panic
 */
void trap_sync(struct trap_frame *frame)
{
    uint64_t esr;
    __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));

    uint32_t ec = (esr >> ESR_EC_SHIFT) & 0x3F;

    if (ec == ESR_EC_SVC64) {
        syscall_handler(frame);
    } else {
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
}

/*
 * Unhandled exception -- just panic.
 */
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
