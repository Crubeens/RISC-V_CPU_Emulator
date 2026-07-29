    .section .text.init
    .globl _start
_start:
    li      t0, -20
    li      t1, 3
    mul     t2, t0, t1
    li      t3, -60
    bne     t2, t3, failure
    div     t2, t0, t1
    li      t3, -6
    bne     t2, t3, failure
    rem     t2, t0, t1
    li      t3, -2
    bne     t2, t3, failure

    li      t0, -1
    mulhu   t2, t0, t0
    li      t3, -2
    bne     t2, t3, failure
    mulhsu  t2, t0, t0
    li      t3, -1
    bne     t2, t3, failure

    li      t0, 0x8000000000000000
    li      t1, 2
    mulh    t2, t0, t1
    li      t3, -1
    bne     t2, t3, failure

    li      t1, -1
    div     t2, t0, t1
    bne     t2, t0, failure
    rem     t2, t0, t1
    bnez    t2, failure

    li      t1, 0
    divu    t2, t0, t1
    li      t3, -1
    bne     t2, t3, failure
    remu    t2, t0, t1
    bne     t2, t0, failure

    li      t0, -1
    li      t1, 2
    divu    t2, t0, t1
    li      t3, 0x7fffffffffffffff
    bne     t2, t3, failure
    remu    t2, t0, t1
    li      t3, 1
    bne     t2, t3, failure

    li      t0, 0x80000000
    li      t1, -1
    divw    t2, t0, t1
    sext.w  t3, t0
    bne     t2, t3, failure
    remw    t2, t0, t1
    bnez    t2, failure
    mulw    t2, t1, t1
    li      t3, 1
    bne     t2, t3, failure
    divuw   t2, t1, t3
    li      t4, -1
    bne     t2, t4, failure
    li      t3, 2
    divuw   t2, t1, t3
    li      t4, 0x7fffffff
    bne     t2, t4, failure
    remuw   t2, t1, t3
    li      t4, 1
    bne     t2, t4, failure

    ebreak

failure:
    .word   0
