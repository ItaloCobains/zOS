/*
 * gic.c -- GICv2 (Generic Interrupt Controller) driver.
 *
 * QEMU virt uses GICv2 with:
 *   - GICD (Distributor) at 0x08000000: routes interrupts to CPUs
 *   - GICC (CPU Interface) at 0x08010000: per-CPU interrupt handling
 *
 * We only need minimal setup:
 *   1. Enable the distributor
 *   2. Enable the CPU interface
 *   3. Enable the timer interrupt (IRQ #30 = non-secure physical timer)
 *   4. Set priority mask to accept all interrupts
 */

#include "types.h"
#include "gic.h"
#include "uart.h"

/* GICD (Distributor) register offsets */
#define GICD_CTLR       (*(volatile uint32_t *)(GICD_BASE + 0x000))
#define GICD_ISENABLER(n) (*(volatile uint32_t *)(GICD_BASE + 0x100 + 4 * (n)))
#define GICD_IPRIORITYR(n) (*(volatile uint32_t *)(GICD_BASE + 0x400 + 4 * (n)))
#define GICD_ITARGETSR(n) (*(volatile uint32_t *)(GICD_BASE + 0x800 + 4 * (n)))

/* GICC (CPU Interface) register offsets */
#define GICC_CTLR       (*(volatile uint32_t *)(GICC_BASE + 0x000))
#define GICC_PMR        (*(volatile uint32_t *)(GICC_BASE + 0x004))
#define GICC_IAR        (*(volatile uint32_t *)(GICC_BASE + 0x00C))
#define GICC_EOIR       (*(volatile uint32_t *)(GICC_BASE + 0x010))

/* Timer IRQ number: non-secure physical timer = PPI #14 = IRQ #30 */
#define TIMER_IRQ   30

void gic_init(void)
{
    /* Enable the Distributor */
    GICD_CTLR = 1;

    /*
     * Enable timer IRQ #30.
     * ISENABLER registers: each bit enables one IRQ.
     * IRQ 30 is bit 30 in ISENABLER[0] (IRQs 0-31).
     */
    GICD_ISENABLER(0) = (1 << TIMER_IRQ);

    /* Also enable virtio-mmio IRQs (48-79 on QEMU virt) */
    GICD_ISENABLER(1) = 0xFFFFFFFF;  /* IRQs 32-63 */
    GICD_ISENABLER(2) = 0xFFFFFFFF;  /* IRQs 64-95 */

    /*
     * Set timer IRQ priority to 0 (highest).
     * IPRIORITYR: each IRQ gets 8 bits of priority. IRQ 30 is in byte 2
     * of IPRIORITYR[7] (30 / 4 = 7, 30 % 4 = 2).
     */
    GICD_IPRIORITYR(7) &= ~(0xFF << 16);  /* Clear byte 2 */

    /*
     * Route timer IRQ to CPU 0.
     * ITARGETSR: each IRQ gets 8 bits. Bit 0 of the target = CPU 0.
     */
    GICD_ITARGETSR(7) |= (1 << 16);  /* Byte 2 = CPU 0 */

    /* Enable CPU Interface */
    GICC_CTLR = 1;

    /* Set priority mask: accept all priorities (0xFF = lowest threshold) */
    GICC_PMR = 0xFF;

    uart_puts("[gic] GICv2 initialized, timer IRQ enabled\n");
}

/*
 * Acknowledge an interrupt: read IAR to get the IRQ number.
 * This also signals to the GIC that we're handling it.
 */
uint32_t gic_acknowledge(void)
{
    return GICC_IAR & 0x3FF;  /* Lower 10 bits = IRQ ID */
}

/*
 * Signal End Of Interrupt to the GIC.
 */
void gic_end_interrupt(uint32_t irq)
{
    GICC_EOIR = irq;
}
