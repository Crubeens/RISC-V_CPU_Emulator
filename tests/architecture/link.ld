OUTPUT_ARCH("riscv")
ENTRY(_start)

/*
 * DUT adaptation of the upstream riscv-tests physical-environment linker
 * script. Fixed section addresses keep tohost stable for the raw-image test
 * runner even when a test (notably rv32uc-rvc) has a large .text.init.
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
