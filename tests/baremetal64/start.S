    .section .text.init
    .globl _start
_start:
    li      t0, -1
    addiw   t1, t0, 1
    bnez    t1, failure

    la      t2, scratch
    sd      t0, 0(t2)
    ld      t3, 0(t2)
    bne     t0, t3, failure

    li      t4, 1
    slli    t4, t4, 63
    srli    t5, t4, 63
    li      t6, 1
    bne     t5, t6, failure

    ebreak

failure:
    .word   0

    .section .bss
    .align 3
scratch:
    .dword  0
