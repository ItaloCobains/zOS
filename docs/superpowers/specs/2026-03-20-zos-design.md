# zOS -- Educational OS for aarch64

## Overview

Educational operating system targeting aarch64, running on QEMU `virt` machine.
Written in C with minimal assembly. Focus on clarity and learning.

## Scope (v1)

- Boot on QEMU virt (aarch64, EL1)
- Serial output via UART (PL011 at 0x09000000)
- Page allocator (bitmap, 4KB pages) + MMU with translation tables
- Preemptive round-robin scheduler with ARM generic timer
- Basic syscalls: write, exit, yield
- Kernel/userspace separation (EL1/EL0)
- Simple userspace program

## Out of Scope (v1)

Filesystem, networking, SMP, drivers beyond UART/timer, interactive shell.

## Architecture

### Boot (boot/start.S, boot/vectors.S)

QEMU virt loads kernel at 0x40000000, starts in EL1.

Entry point:
1. Park secondary cores (only core 0 runs)
2. Setup kernel stack
3. Zero .bss section
4. Call kmain()

Exception vector table dispatches to C handlers for IRQ, SVC, and faults.

### UART Driver (kernel/uart.c)

PL011 at 0x09000000. Functions: uart_init, uart_putc, uart_puts, uart_puthex.

### Memory Management (kernel/mm.c)

Bitmap page allocator. Each bit = one 4KB page.
Free memory starts after kernel image (defined by linker script).
QEMU virt provides 128MB RAM by default.

### MMU (kernel/mmu.c)

4-level translation tables (aarch64).
- Kernel: identity mapped (VA = PA)
- Userspace: mapped at 0x00400000
- Permissions: kernel RWX at EL1, userspace RW/RX at EL0

### Interrupts (kernel/timer.c, kernel/trap.c)

GICv2 for interrupt routing (QEMU virt).
ARM generic timer fires every ~10ms for preemption.
trap.c dispatches: IRQ -> timer -> scheduler, SVC -> syscall handler.

### Scheduler (kernel/sched.c)

Round-robin preemptive. Fixed array of tasks (max 8).
Each task: state, saved registers, own stack, page tables.
Context switch in assembly (save/restore registers).

### Syscalls (kernel/syscall.c)

Userspace calls `svc #0`, syscall number in x8, args in x0-x5.

| # | Name      | Description              |
|---|-----------|--------------------------|
| 0 | sys_write | Write string to UART     |
| 1 | sys_exit  | Terminate task           |
| 2 | sys_yield | Voluntarily yield CPU    |

### Userspace (user/init.c)

Simple program in EL0 using syscalls. Linked separately, embedded in kernel image.

## Boot Flow

```
QEMU -> start.S (EL1) -> kmain() -> uart_init() -> mm_init() -> mmu_init()
-> timer_init() -> gic_init() -> sched_init() -> load userspace task
-> switch to EL0 -> userspace runs -> syscall/timer -> back to kernel
```

## Toolchain

- aarch64-elf-gcc (cross-compiler, Homebrew)
- qemu-system-aarch64
- GNU Make
- GDB (optional debug)

## Directory Structure

```
zOS/
  boot/          -- entry point, exception vectors (assembly)
  kernel/        -- kernel core (C)
  include/       -- headers
  user/          -- userspace programs
  linker.ld      -- linker script
  Makefile
```
