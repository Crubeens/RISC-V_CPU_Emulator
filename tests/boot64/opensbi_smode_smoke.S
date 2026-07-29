    .option norvc
    .section .text.init
    .globl _start

_start:
    # OpenSBI 应传入：
    # a0 = 启动 hart ID，当前应为 0
    # a1 = RV64 DTB 的物理地址，必须非零
    bnez    a0, boot_argument_failure
    beqz    a1, boot_argument_failure

    la      s0, success_message
    j       print_message

boot_argument_failure:
    la      s0, failure_message

print_message:
    lbu     a0, 0(s0)
    beqz    a0, request_shutdown

    # Legacy SBI console_putchar：
    # a7 = 1，a0 = 要输出的字符
    li      a7, 1
    ecall

    addi    s0, s0, 1
    j       print_message

request_shutdown:
    # SBI System Reset 扩展：
    # EID  = 0x53525354
    # FID  = 0
    # a0   = 0，shutdown
    # a1   = 0，no reason
    li      a7, 0x53525354
    li      a6, 0
    li      a0, 0
    li      a1, 0
    ecall

hang:
    wfi
    j       hang

    .section .rodata

success_message:
    .asciz  "RV64 OpenSBI S-mode payload OK\r\n"

failure_message:
    .asciz  "RV64 OpenSBI boot arguments FAILED\r\n"