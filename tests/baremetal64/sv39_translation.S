    .option rvc
    .section .text.init
    .globl _start
_start:
    la      t0, machine_trap
    csrw    mtvec, t0

    # root[256] -> level_one[0] -> level_zero[0..1].
    la      t0, level_one
    srli    t0, t0, 12
    slli    t0, t0, 10
    ori     t0, t0, 1
    la      t1, root_table
    li      t2, (256 * 8)
    add     t1, t1, t2
    sd      t0, 0(t1)

    la      t0, level_zero
    srli    t0, t0, 12
    slli    t0, t0, 10
    ori     t0, t0, 1
    la      t1, level_one
    sd      t0, 0(t1)

    # Spike's default profile faults instead of updating A/D, so this
    # differential image supplies them. Automatic updates are covered by the
    # independent rv64_mmu_tests suite.
    la      t0, supervisor_page
    srli    t0, t0, 12
    slli    t0, t0, 10
    ori     t0, t0, 0x4b       # V | R | X | A
    la      t1, level_zero
    sd      t0, 0(t1)

    la      t0, scratch
    srli    t0, t0, 12
    slli    t0, t0, 10
    ori     t0, t0, 0xc7       # V | R | W | A | D
    sd      t0, 8(t1)

    la      t0, root_table
    srli    t0, t0, 12
    li      t1, (8 << 60)
    or      t0, t0, t1
    csrw    satp, t0
    sfence.vma x0, x0

    li      t0, 0xffffffc000000000
    csrw    mepc, t0
    li      t0, (1 << 11)
    csrw    mstatus, t0
    mret

    .balign 4
machine_trap:
    csrr    t0, mcause
    li      t1, 13
    bne     t0, t1, failure
    csrr    t0, mtval
    li      t1, 0xffffffc000002000
    bne     t0, t1, failure

    la      t0, level_zero
    ld      t1, 0(t0)
    andi    t1, t1, 0x40
    beqz    t1, failure
    ld      t1, 8(t0)
    andi    t1, t1, 0xc0
    li      t2, 0xc0
    bne     t1, t2, failure
    c.ebreak

failure:
    .word   0

    .balign 4096
supervisor_page:
    li      t0, 0xffffffc000001000
    li      t1, 0x1122334455667788
    sd      t1, 0(t0)
    ld      t2, 0(t0)
    bne     t1, t2, failure
    sfence.vma t0, x0
    li      t2, 0xffffffc000002000
    ld      t3, 0(t2)
    j       failure

    .section .data
    .balign 4096
scratch:
    .dword  0

    .section .bss
    .balign 4096
root_table:
    .skip   4096
    .balign 4096
level_one:
    .skip   4096
    .balign 4096
level_zero:
    .skip   4096
