    .option norvc
    .section .text.start, "ax"
    .globl _start

_start:
    lui t0, 0x10000

    li t1, 79
    sb t1, 0(t0)
    li t1, 75
    sb t1, 0(t0)
    li t1, 10
    sb t1, 0(t0)

    lui t0, 0x11100
    li t1, 0x5555
    sw t1, 0(t0)

1:
    wfi
    j 1b
