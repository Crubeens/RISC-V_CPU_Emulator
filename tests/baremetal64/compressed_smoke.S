    .option rvc
    .section .text.init
    .globl _start
_start:
    la          sp, stack_top
    c.addi16sp  sp, 16
    c.addi16sp  sp, -16

    c.lui       s0, 1
    c.li        s0, 1
    c.slli      s0, 63
    c.srli      s0, 63
    li          t0, 1
    bne         s0, t0, failure
    c.li        s0, -1
    c.srai      s0, 63
    li          t0, -1
    bne         s0, t0, failure

    c.li        s0, 5
    c.addi      s0, -1
    c.addiw     s0, 1
    c.slli      s0, 1
    c.srli      s0, 1
    c.srai      s0, 1
    c.andi      s0, 3
    li          t0, 2
    c.mv        s1, t0
    c.add       s0, s1
    c.sub       s0, s1
    c.xor       s0, s1
    c.or        s0, s1
    c.and       s0, s1

    li          s0, -1
    li          s1, 1
    c.addw      s0, s1
    bnez        s0, failure
    c.subw      s0, s1
    li          t0, -1
    bne         s0, t0, failure

    c.addi4spn  s0, sp, 16
    addi        t0, sp, 16
    bne         s0, t0, failure

    la          s0, data_words
    c.ld        s1, 0(s0)
    c.sd        s1, 8(s0)
    ld          t0, 8(s0)
    bne         s1, t0, failure
    c.lw        s1, 0(s0)
    c.sw        s1, 12(s0)
    lw          t0, 12(s0)
    bne         s1, t0, failure

    c.sdsp      s1, 0(sp)
    c.ldsp      t0, 0(sp)
    bne         s1, t0, failure
    c.swsp      s1, 8(sp)
    c.lwsp      t0, 8(sp)
    bne         s1, t0, failure

    c.li        s0, 0
    c.beqz      s0, branch_zero_ok
    j           failure
branch_zero_ok:
    c.li        s0, 1
    c.bnez      s0, branch_nonzero_ok
    j           failure
branch_nonzero_ok:
    c.j         jump_ok
    j           failure
jump_ok:
    la          t0, jalr_target
    c.jalr      t0
    c.j         after_jalr
jalr_target:
    c.jr        ra
after_jalr:
    la          t0, jr_target
    c.jr        t0
    j           failure
jr_target:
    c.nop
    c.ebreak

failure:
    .word       0

    .section .data
    .align 3
data_words:
    .dword      0x1122334480000005
    .dword      0
    .word       0

    .section .bss
    .align 4
stack:
    .space      128
stack_top:
