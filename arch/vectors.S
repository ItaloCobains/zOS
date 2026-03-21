/*
 * vectors.S -- Exception vector table for aarch64.
 *
 * ARM64 has 16 exception vectors, grouped into 4 sets of 4 entries:
 *   1. Current EL with SP_EL0       (not used -- kernel uses SP_EL1)
 *   2. Current EL with SP_ELx       (kernel-to-kernel exceptions)
 *   3. Lower EL using AArch64       (userspace-to-kernel exceptions)
 *   4. Lower EL using AArch32       (not used)
 *
 * Each entry is exactly 128 bytes (0x80). We only care about:
 *   - Synchronous from lower EL (syscalls via SVC)
 *   - IRQ from lower EL (timer interrupts)
 *   - Synchronous from current EL (kernel faults -- panic)
 *   - IRQ from current EL (timer interrupts while in kernel)
 *
 * On entry we save all 31 general-purpose registers plus SP_EL0, ELR_EL1,
 * and SPSR_EL1 onto the kernel stack. This forms a trap_frame struct that
 * C handlers can access.
 */

/*
 * TRAP_FRAME_SIZE = 31 regs * 8 + sp(8) + elr(8) + spsr(8) = 272 bytes.
 * We round up to 288 (multiple of 16) for stack alignment.
 */
#define TRAP_FRAME_SIZE 288

/*
 * Macro: save all registers onto the stack, forming a trap_frame.
 */
.macro save_regs
    sub     sp, sp, #TRAP_FRAME_SIZE

    /* Save x0-x29 in pairs */
    stp     x0,  x1,  [sp, #0]
    stp     x2,  x3,  [sp, #16]
    stp     x4,  x5,  [sp, #32]
    stp     x6,  x7,  [sp, #48]
    stp     x8,  x9,  [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x19, [sp, #144]
    stp     x20, x21, [sp, #160]
    stp     x22, x23, [sp, #176]
    stp     x24, x25, [sp, #192]
    stp     x26, x27, [sp, #208]
    stp     x28, x29, [sp, #224]

    /* Save x30 (link register) */
    str     x30, [sp, #240]

    /* Save SP_EL0 (userspace stack pointer) */
    mrs     x0, sp_el0
    str     x0, [sp, #248]

    /* Save ELR_EL1 (return address) and SPSR_EL1 (saved status) */
    mrs     x0, elr_el1
    str     x0, [sp, #256]
    mrs     x0, spsr_el1
    str     x0, [sp, #264]
.endm

/*
 * Macro: restore all registers from the stack trap_frame.
 */
.macro restore_regs
    /* Restore SPSR_EL1 and ELR_EL1 */
    ldr     x0, [sp, #264]
    msr     spsr_el1, x0
    ldr     x0, [sp, #256]
    msr     elr_el1, x0

    /* Restore SP_EL0 */
    ldr     x0, [sp, #248]
    msr     sp_el0, x0

    /* Restore x30 */
    ldr     x30, [sp, #240]

    /* Restore x0-x29 in pairs */
    ldp     x28, x29, [sp, #224]
    ldp     x26, x27, [sp, #208]
    ldp     x24, x25, [sp, #192]
    ldp     x22, x23, [sp, #176]
    ldp     x20, x21, [sp, #160]
    ldp     x18, x19, [sp, #144]
    ldp     x16, x17, [sp, #128]
    ldp     x14, x15, [sp, #112]
    ldp     x12, x13, [sp, #96]
    ldp     x10, x11, [sp, #80]
    ldp     x8,  x9,  [sp, #64]
    ldp     x6,  x7,  [sp, #48]
    ldp     x4,  x5,  [sp, #32]
    ldp     x2,  x3,  [sp, #16]
    ldp     x0,  x1,  [sp, #0]

    add     sp, sp, #TRAP_FRAME_SIZE
.endm

/* ---------------------------------------------------------------
 * Exception vector table.
 *
 * Must be aligned to 2048 bytes (0x800).
 * Each entry is 128 bytes (0x80) -- enough for a branch.
 * --------------------------------------------------------------- */
.section .text
.balign 2048
.global vectors
vectors:

/* ---- Current EL with SP_EL0 (unused) ---- */
.balign 128
    b       _trap_error         /* Synchronous */
.balign 128
    b       _trap_error         /* IRQ */
.balign 128
    b       _trap_error         /* FIQ */
.balign 128
    b       _trap_error         /* SError */

/* ---- Current EL with SP_ELx (kernel exceptions) ---- */
.balign 128
    b       _trap_sync_current  /* Synchronous */
.balign 128
    b       _trap_irq_current   /* IRQ */
.balign 128
    b       _trap_error         /* FIQ */
.balign 128
    b       _trap_error         /* SError */

/* ---- Lower EL using AArch64 (userspace exceptions) ---- */
.balign 128
    b       _trap_sync_lower    /* Synchronous (syscalls land here) */
.balign 128
    b       _trap_irq_lower     /* IRQ (timer interrupts from userspace) */
.balign 128
    b       _trap_error         /* FIQ */
.balign 128
    b       _trap_error         /* SError */

/* ---- Lower EL using AArch32 (unused) ---- */
.balign 128
    b       _trap_error
.balign 128
    b       _trap_error
.balign 128
    b       _trap_error
.balign 128
    b       _trap_error

/* ---------------------------------------------------------------
 * Exception handlers -- save state, call C, restore, return.
 * --------------------------------------------------------------- */

_trap_sync_lower:
    save_regs
    mov     x0, sp              /* x0 = pointer to trap_frame */
    bl      trap_sync           /* call C handler */
    restore_regs
    eret                        /* return to EL0 */

_trap_irq_lower:
    save_regs
    mov     x0, sp
    bl      trap_irq
    restore_regs
    eret

_trap_sync_current:
    save_regs
    mov     x0, sp
    bl      trap_sync
    restore_regs
    eret

_trap_irq_current:
    save_regs
    mov     x0, sp
    bl      trap_irq
    restore_regs
    eret

_trap_error:
    save_regs
    mov     x0, sp
    bl      trap_error
    b       .                   /* hang -- trap_error should never return */

/* ---------------------------------------------------------------
 * switch_to_user -- Starts or resumes a userspace task.
 *
 * Called from C: switch_to_user(struct trap_frame *frame)
 * Loads the trap_frame into registers and does ERET to EL0.
 * --------------------------------------------------------------- */
.global switch_to_user
switch_to_user:
    mov     sp, x0              /* point SP at the trap_frame */
    restore_regs
    eret
