/*
 * zOS Linker Script for aarch64 QEMU virt machine.
 *
 * QEMU virt loads the kernel at 0x40080000 (membase 0x40000000 + 0x80000).
 * This is the standard aarch64 kernel load offset.
 * We lay out sections sequentially: .text, .rodata, .data, .bss.
 * Symbols _bss_start/_bss_end are used by start.S to zero BSS.
 * _kernel_end marks where free memory begins for the page allocator.
 */

ENTRY(_start)

SECTIONS
{
    /* QEMU virt loads us here (membase + 0x80000) */
    . = 0x40080000;

    .text : {
        *(.text.boot)    /* boot code first -- _start must be at load address */
        *(.text .text.*)
    }

    . = ALIGN(4096);
    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
    }

    . = ALIGN(4096);
    _bss_start = .;
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }
    _bss_end = .;

    . = ALIGN(4096);

    /* Embedded userspace binary */
    .user : {
        _user_start = .;
        *(.user)
        _user_end = .;
    }

    . = ALIGN(4096);

    .user2 : {
        _user2_start = .;
        *(.user2)
        _user2_end = .;
    }

    . = ALIGN(4096);
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
