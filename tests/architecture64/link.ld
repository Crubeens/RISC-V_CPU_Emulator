OUTPUT_ARCH("riscv")
ENTRY(_start)

/*
 * Fixed addresses match the architecture runner's raw DRAM image and keep
 * the upstream tohost result word independent of individual test size.
 */
SECTIONS
{
  . = 0x80000000;
  .text.init : { *(.text.init) }

  . = 0x80010000;
  .tohost : { *(.tohost) }

  . = 0x80011000;
  .text : { *(.text) }

  . = 0x80020000;
  .data : { *(.data) }
  .bss : { *(.bss) }
  _end = .;
}
