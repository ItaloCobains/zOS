# zOS Makefile
#
# Cross-compiles the kernel for aarch64 and runs it on QEMU.
# Requirements: aarch64-elf-gcc, aarch64-elf-ld, aarch64-elf-objcopy, qemu-system-aarch64

CROSS   = aarch64-elf-
CC      = $(CROSS)gcc
AS      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

CFLAGS  = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
          -mgeneral-regs-only -Iinclude -O1 -g
ASFLAGS = -Iinclude

# Source files
BOOT_SRC  = boot/start.S boot/vectors.S
KERN_SRC  = kernel/main.c kernel/uart.c kernel/mm.c kernel/mmu.c \
            kernel/timer.c kernel/gic.c kernel/trap.c kernel/sched.c \
            kernel/syscall.c kernel/string.c
USER_SRC  = user/init.c user/syscall_stub.S

# Object files
BOOT_OBJ  = $(BOOT_SRC:.S=.o)
KERN_OBJ  = $(KERN_SRC:.c=.o)
USER_OBJ  = $(USER_SRC:.c=.o)
USER_OBJ := $(USER_OBJ:.S=.o)

# Targets
KERNEL    = kernel.elf
KERNEL_BIN = kernel.bin
USER_ELF  = user/init.elf
USER_BIN  = user/init.bin
USER2_ELF = user/shell.elf
USER2_BIN = user/shell.bin

.PHONY: all clean run debug

all: $(KERNEL_BIN)

# --- Userspace program ---
# Built separately with its own link address

user/syscall_stub.o: user/syscall_stub.S
	$(AS) $(ASFLAGS) -c $< -o $@

user/init.o: user/init.c
	$(CC) $(CFLAGS) -c $< -o $@

$(USER_ELF): user/syscall_stub.o user/init.o user/user.ld
	$(LD) -T user/user.ld user/syscall_stub.o user/init.o -o $@

$(USER_BIN): $(USER_ELF)
	$(OBJCOPY) -O binary $< $@

# Convert userspace binary to a linkable object so we can embed it in the kernel
user/init_blob.o: $(USER_BIN)
	$(OBJCOPY) -I binary -O elf64-littleaarch64 \
		--rename-section .data=.user \
		$< $@

user/syscall_stub2.o: user/syscall_stub2.S
	$(AS) $(ASFLAGS) -c $< -o $@

user/shell.o: user/shell.c
	$(CC) $(CFLAGS) -Iuser -c $< -o $@

user/printf.o: user/printf.c
	$(CC) $(CFLAGS) -c $< -o $@

$(USER2_ELF): user/syscall_stub2.o user/shell.o user/printf.o user/user2.ld
	$(LD) -T user/user2.ld user/syscall_stub2.o user/shell.o user/printf.o -o $@

$(USER2_BIN): $(USER2_ELF)
	$(OBJCOPY) -O binary $< $@

user/shell_blob.o: $(USER2_BIN)
	$(OBJCOPY) -I binary -O elf64-littleaarch64 \
		--rename-section .data=.user2 \
		$< $@

# --- Kernel ---

%.o: %.S
	$(AS) $(ASFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(BOOT_OBJ) $(KERN_OBJ) user/init_blob.o user/shell_blob.o linker.ld
	$(LD) -T linker.ld $(BOOT_OBJ) $(KERN_OBJ) user/init_blob.o user/shell_blob.o -o $@

$(KERNEL_BIN): $(KERNEL)
	$(OBJCOPY) -O binary $< $@

# --- Run ---

run: $(KERNEL_BIN)
	qemu-system-aarch64 \
		-machine virt \
		-cpu cortex-a72 \
		-m 128M \
		-nographic \
		-kernel $(KERNEL_BIN)

debug: $(KERNEL_BIN)
	qemu-system-aarch64 \
		-machine virt \
		-cpu cortex-a72 \
		-m 128M \
		-nographic \
		-kernel $(KERNEL_BIN) \
		-S -s

clean:
	rm -f $(BOOT_OBJ) $(KERN_OBJ) $(USER_OBJ) user/init_blob.o user/syscall_stub2.o user/shell.o user/printf.o user/shell_blob.o
	rm -f $(KERNEL) $(KERNEL_BIN) $(USER_ELF) $(USER_BIN) $(USER2_ELF) $(USER2_BIN)
