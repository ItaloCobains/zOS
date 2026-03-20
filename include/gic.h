#ifndef ZOS_GIC_H
#define ZOS_GIC_H

#include "types.h"

/* GICv2 base addresses for QEMU virt machine */
#define GICD_BASE 0x08000000
#define GICC_BASE 0x08010000

void     gic_init(void);
uint32_t gic_acknowledge(void);
void     gic_end_interrupt(uint32_t irq);

#endif
