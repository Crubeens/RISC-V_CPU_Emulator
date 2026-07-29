    .section .text.init
    .globl _start
_start:
    la      t0, scratch_d
    li      t1, 0x1122334455667788
    sd      t1, 0(t0)
    lr.d.aq t2, (t0)
    bne     t2, t1, failure
    li      t3, 0x8877665544332211
    sc.d.rl t4, t3, (t0)
    bnez    t4, failure
    ld      t5, 0(t0)
    bne     t5, t3, failure
    sc.d    t4, t1, (t0)
    beqz    t4, failure

    li      t1, 0x8000000000000005
    li      t2, 3

    sd      t1, 0(t0)
    amoswap.d t3, t2, (t0)
    ld      t4, 0(t0)

    sd      t1, 0(t0)
    amoadd.d.aqrl t3, t2, (t0)
    ld      t4, 0(t0)

    sd      t1, 0(t0)
    amoxor.d t3, t2, (t0)
    ld      t4, 0(t0)

    sd      t1, 0(t0)
    amoand.d t3, t2, (t0)
    ld      t4, 0(t0)

    sd      t1, 0(t0)
    amoor.d t3, t2, (t0)
    ld      t4, 0(t0)

    sd      t1, 0(t0)
    amomin.d t3, t2, (t0)
    ld      t4, 0(t0)

    sd      t1, 0(t0)
    amomax.d t3, t2, (t0)
    ld      t4, 0(t0)

    sd      t1, 0(t0)
    amominu.d t3, t2, (t0)
    ld      t4, 0(t0)

    sd      t1, 0(t0)
    amomaxu.d t3, t2, (t0)
    ld      t4, 0(t0)

    la      t0, scratch_w
    li      t1, 0x80000005
    sw      t1, 0(t0)
    lr.w    t3, (t0)
    li      t4, -2147483643
    bne     t3, t4, failure
    sc.w    t4, t2, (t0)
    bnez    t4, failure

    sw      t1, 0(t0)
    amoswap.w t3, t2, (t0)
    lw      t4, 0(t0)

    sw      t1, 0(t0)
    amoadd.w t3, t2, (t0)
    lw      t4, 0(t0)

    sw      t1, 0(t0)
    amoxor.w t3, t2, (t0)
    lw      t4, 0(t0)

    sw      t1, 0(t0)
    amoand.w t3, t2, (t0)
    lw      t4, 0(t0)

    sw      t1, 0(t0)
    amoor.w t3, t2, (t0)
    lw      t4, 0(t0)

    sw      t1, 0(t0)
    amomin.w t3, t2, (t0)
    lw      t4, 0(t0)

    sw      t1, 0(t0)
    amomax.w t3, t2, (t0)
    lw      t4, 0(t0)

    sw      t1, 0(t0)
    amominu.w t3, t2, (t0)
    lw      t4, 0(t0)

    sw      t1, 0(t0)
    amomaxu.w t3, t2, (t0)
    lw      t4, 0(t0)

    ebreak

failure:
    .word   0

    .section .data
    .align 3
scratch_d:
    .dword  0
scratch_w:
    .word   0
