/*
 * 演示程序：
 * 分别创建 imem 和 dmem，向 imem 装入 ADDI、SW、LW、BEQ 指令，
 * 逐步运行 CPU 并打印状态，最后检查访存、写回和分支结果。
 */

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>

#include "rv32/cpu.h"
#include "rv32/dmem.h"
#include "rv32/imem.h"

#define IMEM_SIZE 64u
#define DMEM_SIZE 64u

static const char *status_name(rv32_status_t status)
{
    switch (status) {
        case RV32_OK:
            return "RV32_OK";
        case RV32_ERR_BAD_ARGUMENT:
            return "RV32_ERR_BAD_ARGUMENT";
        case RV32_ERR_MEMORY:
            return "RV32_ERR_MEMORY";
        case RV32_ERR_ILLEGAL_INSTRUCTION:
            return "RV32_ERR_ILLEGAL_INSTRUCTION";
        default:
            return "RV32_ERR_UNKNOWN";
    }
}

int main(void)
{
    static const uint32_t program[] = {
        0x01000093u, /* ADDI x1, x0, 16 */
        0x02a00113u, /* ADDI x2, x0, 42 */
        0x0020a023u, /* SW   x2, 0(x1)   */
        0x0000a183u, /* LW   x3, 0(x1)   */
        0x00310463u, /* BEQ  x2, x3, +8  */
        0x00100213u, /* ADDI x4, x0, 1（应跳过） */
        0x00200213u  /* ADDI x4, x0, 2   */
    };
    uint8_t imem_data[IMEM_SIZE] = {0};
    uint8_t dmem_data[DMEM_SIZE] = {0};
    rv32_imem_t imem = {imem_data, IMEM_SIZE};
    rv32_dmem_t dmem = {dmem_data, DMEM_SIZE};
    rv32_cpu_t cpu;
    rv32_status_t status;
    uint32_t stored_value;
    size_t i;

    for (i = 0; i < sizeof(program) / sizeof(program[0]); ++i) {
        status = rv32_imem_write_u32(
            &imem,
            (uint32_t)(i * sizeof(uint32_t)),
            program[i]);
        if (status != RV32_OK) {
            fprintf(stderr, "program load failed: %s\n", status_name(status));
            return 1;
        }
    }

    status = rv32_cpu_init(&cpu, &imem, &dmem, 0u);
    if (status != RV32_OK) {
        fprintf(stderr, "cpu init failed: %s\n", status_name(status));
        return 1;
    }

    for (i = 0; i < 6u; ++i) {
        uint32_t old_pc = cpu.pc;

        status = rv32_cpu_step(&cpu);
        if (status != RV32_OK) {
            fprintf(
                stderr,
                "step failed at pc=0x%08" PRIx32 ": %s\n",
                old_pc,
                status_name(status));
            return 1;
        }

        printf(
            "pc 0x%08" PRIx32 " -> 0x%08" PRIx32
            " | x1=%" PRIu32 " x2=%" PRIu32
            " x3=%" PRIu32 " x4=%" PRIu32 "\n",
            old_pc,
            cpu.pc,
            cpu.regs[1],
            cpu.regs[2],
            cpu.regs[3],
            cpu.regs[4]);
    }

    status = rv32_dmem_read_u32(&dmem, 16u, &stored_value);
    if (status != RV32_OK ||
        stored_value != 42u ||
        cpu.regs[3] != 42u ||
        cpu.regs[4] != 2u) {
        fprintf(stderr, "final state check failed\n");
        return 1;
    }

    puts("demo passed");
    return 0;
}
