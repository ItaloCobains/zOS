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
          -mgeneral-regs-only -Iuser/lib -O1 -g

# Architecture (assembly)
ARCH_SRC = arch/start.S arch/vectors.S
ARCH_OBJ = $(ARCH_SRC:.S=.o)

# Kernel core
KERN_SRC = kernel/main.c kernel/sched.c kernel/syscall.c \
           kernel/trap.c kernel/mm.c kernel/mmu.c kernel/string.c

# Drivers
DRV_SRC  = drivers/uart.c drivers/gic.c drivers/timer.c

# Filesystem
FS_SRC   = fs/vfs.c fs/devfs.c

KERN_OBJ = $(KERN_SRC:.c=.o)
DRV_OBJ  = $(DRV_SRC:.c=.o)
FS_OBJ   = $(FS_SRC:.c=.o)
ALL_KERN = $(ARCH_OBJ) $(KERN_OBJ) $(DRV_OBJ) $(FS_OBJ)

# User programs
USER_BINS    = shell ls cat echo hello ps touch mkdir rm
USER_COMMON  = user/lib/crt0.o user/lib/syscalls.o user/lib/ulib.o

# Blob objects embedded in the kernel
BLOB_OBJ = $(foreach b,$(USER_BINS),user/bin/$(b)_blob.o)

KERNEL     = kernel.elf
KERNEL_BIN = kernel.bin

.PHONY: all clean run debug

all: $(KERNEL_BIN)

# --- User library ---

user/lib/crt0.o: user/lib/crt0.S
	$(AS) $(ASFLAGS) -c $< -o $@

user/lib/syscalls.o: user/lib/syscalls.S
	$(AS) $(ASFLAGS) -c $< -o $@

user/lib/ulib.o: user/lib/ulib.c
	$(CC) $(UCFLAGS) -c $< -o $@

# --- User binaries ---

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

arch/%.o: arch/%.S
	$(AS) $(ASFLAGS) -c $< -o $@

kernel/%.o: kernel/%.c
	$(CC) $(CFLAGS) -c $< -o $@

drivers/%.o: drivers/%.c
	$(CC) $(CFLAGS) -c $< -o $@

fs/%.o: fs/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL): $(ALL_KERN) $(BLOB_OBJ) kernel.ld
	$(LD) -T kernel.ld $(ALL_KERN) $(BLOB_OBJ) -o $@

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
	rm -f $(ALL_KERN) $(USER_COMMON) $(BLOB_OBJ)
	rm -f user/bin/*.o user/bin/*.elf user/bin/*.bin
	rm -f $(KERNEL) $(KERNEL_BIN)
