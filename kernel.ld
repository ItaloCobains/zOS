/*
 * zOS Linker Script for aarch64 QEMU virt machine.
 *
 * QEMU virt loads the kernel at 0x40080000 (membase 0x40000000 + 0x80000).
 */

ENTRY(_start)

SECTIONS
{
    . = 0x40080000;

    .text : {
        *(.text.boot)
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

    /* Embedded userspace binaries */
    .bin_shell : {
        _bin_shell_start = .;
        *(.bin_shell)
        _bin_shell_end = .;
    }
    . = ALIGN(4096);
    .bin_ls : {
        _bin_ls_start = .;
        *(.bin_ls)
        _bin_ls_end = .;
    }
    . = ALIGN(4096);
    .bin_cat : {
        _bin_cat_start = .;
        *(.bin_cat)
        _bin_cat_end = .;
    }
    . = ALIGN(4096);
    .bin_echo : {
        _bin_echo_start = .;
        *(.bin_echo)
        _bin_echo_end = .;
    }
    . = ALIGN(4096);
    .bin_hello : {
        _bin_hello_start = .;
        *(.bin_hello)
        _bin_hello_end = .;
    }
    . = ALIGN(4096);
    .bin_ps : {
        _bin_ps_start = .;
        *(.bin_ps)
        _bin_ps_end = .;
    }
    . = ALIGN(4096);
    .bin_touch : {
        _bin_touch_start = .;
        *(.bin_touch)
        _bin_touch_end = .;
    }
    . = ALIGN(4096);
    .bin_mkdir : {
        _bin_mkdir_start = .;
        *(.bin_mkdir)
        _bin_mkdir_end = .;
    }
    . = ALIGN(4096);
    .bin_rm : {
        _bin_rm_start = .;
        *(.bin_rm)
        _bin_rm_end = .;
    }
    . = ALIGN(4096);
    .bin_edit : {
        _bin_edit_start = .;
        *(.bin_edit)
        _bin_edit_end = .;
    }
    . = ALIGN(4096);
    .bin_login : {
        _bin_login_start = .;
        *(.bin_login)
        _bin_login_end = .;
    }

    . = ALIGN(4096);
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
