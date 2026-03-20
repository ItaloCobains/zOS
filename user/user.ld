/*
 * Linker script for userspace programs.
 * Programs are loaded at virtual address 0x00400000 in EL0.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x00400000;

    .text : {
        *(.text.boot)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        *(.bss .bss.*)
    }
}
