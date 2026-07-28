    .option rvc
    .section .text.start, "ax"
    .globl _start

_start:
    la sp, _stack_top
    c.li a0, 0

    # Integer immediate and register forms.
    c.li s0, 5
    c.addi s0, -1
    c.slli s0, 2
    c.srli s0, 1
    c.srai s0, 1
    c.andi s0, 3
    c.bnez s0, fail

    c.li s1, 7
    c.mv a1, s1
    c.add a1, s1
    c.sub a1, s1
    c.xor a1, s1
    c.or a1, s1
    c.and a1, s1
    c.li a2, 7
    bne a1, a2, fail

    c.lui s0, 1
    lui s1, 1
    bne s0, s1, fail

    # Stack-relative and compact-register memory forms.
    c.addi16sp sp, -16
    c.li a2, 30
    c.swsp a2, 0(sp)
    c.lwsp a3, 0(sp)
    bne a2, a3, fail

    c.addi4spn a4, sp, 4
    c.sw a2, 0(a4)
    c.lw a5, 0(a4)
    bne a2, a5, fail
    c.addi16sp sp, 16

    # All compressed control-transfer forms.
    c.li s0, 0
    c.beqz s0, 1f
    c.j fail
1:
    c.li s0, 1
    c.bnez s0, 2f
    c.j fail
2:
    c.jal jal_target
after_jal:
    la t0, jalr_target
    c.jalr t0
after_jalr:
    la t0, jr_target
    c.jr t0
    c.j fail

jr_target:
    c.nop
    c.j pass

jal_target:
    c.jr ra

jalr_target:
    c.jr ra

fail:
    c.li a0, 1
    c.ebreak

pass:
    c.li a0, 0
    c.ebreak
