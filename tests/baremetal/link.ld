OUTPUT_ARCH(riscv)
ENTRY(_start)

MEMORY
{
    RAM (rwx) : ORIGIN = 0x80000000, LENGTH = 1M
}

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = ORIGIN(RAM);

    .text : ALIGN(4)
    {
        KEEP(*(.text.start))
        *(.text .text.*)
    } > RAM :text

    .rodata : ALIGN(4)
    {
        *(.rodata .rodata.*)
    } > RAM :text

    .data : ALIGN(4)
    {
        *(.sdata .sdata.*)
        *(.data .data.*)
    } > RAM :data

    .bss (NOLOAD) : ALIGN(4)
    {
        _bss_start = .;
        *(.sbss .sbss.*)
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > RAM :data

    _stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
