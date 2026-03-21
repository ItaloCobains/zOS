# zOS Makefile

CROSS   = aarch64-elf-
CC      = $(CROSS)gcc
AS      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

CFLAGS  = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
          -mgeneral-regs-only -Iinclude -O1 -g
ASFLAGS = -Iinclude
UCFLAGS = -Wall -Wextra -ffreestanding -nostdlib -nostartfiles \
          -mgeneral-regs-only -Iuser -O1 -g

# Kernel sources
BOOT_SRC  = boot/start.S boot/vectors.S
KERN_SRC  = kernel/main.c kernel/uart.c kernel/mm.c kernel/mmu.c \
            kernel/timer.c kernel/gic.c kernel/trap.c kernel/sched.c \
            kernel/syscall.c kernel/string.c kernel/vfs.c kernel/devfs.c

BOOT_OBJ  = $(BOOT_SRC:.S=.o)
KERN_OBJ  = $(KERN_SRC:.c=.o)

# User programs (each becomes a separate binary in /bin/)
USER_BINS  = shell ls cat echo hello ps touch mkdir rm
USER_COMMON = user/crt0.o user/syscalls.o user/ulib.o

# Targets
KERNEL     = kernel.elf
KERNEL_BIN = kernel.bin

# Blob objects to embed in the kernel
BLOB_OBJ   = $(foreach b,$(USER_BINS),user/bin/$(b)_blob.o)

.PHONY: all clean run debug

all: $(KERNEL_BIN)

# --- User common objects ---

user/crt0.o: user/crt0.S
	$(AS) $(ASFLAGS) -c $< -o $@

user/syscalls.o: user/syscalls.S
	$(AS) $(ASFLAGS) -c $< -o $@

user/ulib.o: user/ulib.c
	$(CC) $(UCFLAGS) -c $< -o $@

# --- Build each user binary ---
# Pattern: user/bin/X.c -> user/bin/X.o -> user/bin/X.elf -> user/bin/X.bin -> user/bin/X_blob.o

user/bin/%.o: user/bin/%.c
	$(CC) $(UCFLAGS) -c $< -o $@

user/bin/%.elf: user/bin/%.o $(USER_COMMON) user/user.ld
	$(LD) -T user/user.ld $(USER_COMMON) $< -o $@

user/bin/%.bin: user/bin/%.elf
	$(OBJCOPY) -O binary $< $@

user/bin/%_blob.o: user/bin/%.bin
	$(OBJCOPY) -I binary -O elf64-littleaarch64 \
		--rename-section .data=.bin_$* \
		$< $@

# --- Kernel ---

%.o: %.S
	$(AS) $(ASFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(BOOT_OBJ) $(KERN_OBJ) $(BLOB_OBJ) linker.ld
	$(LD) -T linker.ld $(BOOT_OBJ) $(KERN_OBJ) $(BLOB_OBJ) -o $@

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
	rm -f $(BOOT_OBJ) $(KERN_OBJ) $(USER_COMMON) $(BLOB_OBJ)
	rm -f user/bin/*.o user/bin/*.elf user/bin/*.bin
	rm -f $(KERNEL) $(KERNEL_BIN)
