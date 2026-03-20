/*
 * start.S -- Kernel entry point for aarch64.
 *
 * QEMU virt loads us at 0x40080000 and starts all cores in EL1.
 * We park secondary cores (only core 0 proceeds), set up the stack,
 * zero .bss, and jump to kmain() in C.
 */

.section .text.boot
.global _start

_start:
    /* ---------------------------------------------------------------
     * Which core are we? Read MPIDR_EL1 (Multiprocessor Affinity Register).
     * Bits [1:0] give the core ID on QEMU virt.
     * Only core 0 continues; others loop forever.
     * --------------------------------------------------------------- */
    mrs     x0, mpidr_el1
    and     x0, x0, #0xFF
    cbz     x0, primary_core

    /* Secondary cores: spin in a low-power loop */
halt:
    wfe
    b       halt

primary_core:
    /* ---------------------------------------------------------------
     * Set up the kernel stack. Place it 16KB after the end of the
     * kernel image. The stack grows downward, so SP starts at the top.
     * _kernel_end is defined by the linker script.
     * --------------------------------------------------------------- */
    ldr     x0, =_kernel_end
    add     x0, x0, #0x4000        /* 16KB kernel stack */
    mov     sp, x0

    /* ---------------------------------------------------------------
     * Install our exception vector table.
     * VBAR_EL1 points to the vector table defined in vectors.S.
     * --------------------------------------------------------------- */
    ldr     x0, =vectors
    msr     vbar_el1, x0

    /* ---------------------------------------------------------------
     * Zero the .bss section.
     * Symbols _bss_start and _bss_end come from the linker script.
     * --------------------------------------------------------------- */
    ldr     x0, =_bss_start
    ldr     x1, =_bss_end
bss_loop:
    cmp     x0, x1
    b.ge    bss_done
    str     xzr, [x0], #8
    b       bss_loop
bss_done:

    /* ---------------------------------------------------------------
     * Jump to C code. kmain() should never return, but if it does
     * we hang.
     * --------------------------------------------------------------- */
    bl      kmain
    b       halt
