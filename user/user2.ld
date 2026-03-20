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