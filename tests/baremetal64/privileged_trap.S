    .option rvc
    .section .text.init
    .globl _start
_start:
    la      t0, machine_trap
    csrw    mtvec, t0
    la      t0, supervisor_trap
    csrw    stvec, t0

    li      t0, (1 << 9)
    csrw    medeleg, t0
    li      t0, (1 << 7)
    csrw    mie, t0

    li      t0, 0x02004000
    li      t1, 1000
    sd      t1, 0(t0)

    la      t0, supervisor_entry
    csrw    mepc, t0
    li      t0, (1 << 11)
    csrw    mstatus, t0
    mret

supervisor_entry:
    ecall
after_supervisor_ecall:
    wfi
after_wfi:
    csrr    t0, mstatus
after_illegal_csr:
    c.ebreak

supervisor_trap:
    csrr    t0, scause
    li      t1, 9
    bne     t0, t1, failure
    csrr    t0, sepc
    addi    t0, t0, 4
    csrw    sepc, t0
    sret

machine_trap:
    csrr    t0, mcause
    li      t1, 0x8000000000000007
    beq     t0, t1, timer_interrupt
    li      t1, 2
    bne     t0, t1, failure
    csrr    t0, mepc
    addi    t0, t0, 4
    csrw    mepc, t0
    mret

timer_interrupt:
    li      t0, 0x02004000
    li      t1, -1
    sd      t1, 0(t0)
    mret

failure:
    .word   0
