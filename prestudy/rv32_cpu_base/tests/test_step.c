/*
 * CPU 单步测试：
 * 检查完整取指执行流程、imem/dmem 分工、分支、x0 恒为零，
 * 以及访存失败时不提交 PC 和寄存器修改。
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "rv32/cpu.h"
#include "rv32/dmem.h"
#include "rv32/imem.h"

static int failures;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                              \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static void test_program(void)
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
    uint8_t imem_data[64] = {0};
    uint8_t dmem_data[64] = {0};
    rv32_imem_t imem = {imem_data, sizeof(imem_data)};
    rv32_dmem_t dmem = {dmem_data, sizeof(dmem_data)};
    rv32_cpu_t cpu;
    uint32_t value = 0u;
    size_t i;

    for (i = 0; i < sizeof(program) / sizeof(program[0]); ++i) {
        CHECK(rv32_imem_write_u32(
                  &imem,
                  (uint32_t)(i * sizeof(uint32_t)),
                  program[i]) == RV32_OK);
    }

    /* dmem[0] 故意不是有效指令，证明取指不经过 dmem。 */
    CHECK(rv32_dmem_write_u32(&dmem, 0u, 0xffffffffu) == RV32_OK);
    CHECK(rv32_cpu_init(&cpu, &imem, &dmem, 0u) == RV32_OK);

    CHECK(rv32_cpu_step(&cpu) == RV32_OK);
    CHECK(cpu.pc == 4u);
    CHECK(cpu.regs[1] == 16u);

    CHECK(rv32_cpu_step(&cpu) == RV32_OK);
    CHECK(cpu.regs[2] == 42u);

    CHECK(rv32_cpu_step(&cpu) == RV32_OK);
    CHECK(rv32_dmem_read_u32(&dmem, 16u, &value) == RV32_OK);
    CHECK(value == 42u);
    CHECK(rv32_imem_read_u32(&imem, 16u, &value) == RV32_OK);
    CHECK(value == program[4]);

    CHECK(rv32_cpu_step(&cpu) == RV32_OK);
    CHECK(cpu.regs[3] == 42u);

    CHECK(rv32_cpu_step(&cpu) == RV32_OK);
    CHECK(cpu.pc == 24u);

    CHECK(rv32_cpu_step(&cpu) == RV32_OK);
    CHECK(cpu.regs[4] == 2u);
    CHECK(cpu.regs[0] == 0u);
}

static void test_x0_is_immutable(void)
{
    uint8_t imem_data[4] = {0};
    uint8_t dmem_data[4] = {0};
    rv32_imem_t imem = {imem_data, sizeof(imem_data)};
    rv32_dmem_t dmem = {dmem_data, sizeof(dmem_data)};
    rv32_cpu_t cpu;

    CHECK(rv32_imem_write_u32(&imem, 0u, 0x00500013u) == RV32_OK);
    CHECK(rv32_cpu_init(&cpu, &imem, &dmem, 0u) == RV32_OK);
    CHECK(rv32_cpu_step(&cpu) == RV32_OK);
    CHECK(cpu.regs[0] == 0u);
}

static void test_failed_load_does_not_commit(void)
{
    uint8_t imem_data[4] = {0};
    uint8_t dmem_data[4] = {0};
    rv32_imem_t imem = {imem_data, sizeof(imem_data)};
    rv32_dmem_t dmem = {dmem_data, sizeof(dmem_data)};
    rv32_cpu_t cpu;

    /* LW x1, 0(x2)，x2 指向 dmem 末尾之外。 */
    CHECK(rv32_imem_write_u32(&imem, 0u, 0x00012083u) == RV32_OK);
    CHECK(rv32_cpu_init(&cpu, &imem, &dmem, 0u) == RV32_OK);
    cpu.regs[1] = 99u;
    cpu.regs[2] = 4u;

    CHECK(rv32_cpu_step(&cpu) == RV32_ERR_MEMORY);
    CHECK(cpu.pc == 0u);
    CHECK(cpu.regs[1] == 99u);
}

int main(void)
{
    test_program();
    test_x0_is_immutable();
    test_failed_load_does_not_commit();

    if (failures == 0) {
        puts("test_step passed");
    }
    return failures == 0 ? 0 : 1;
}
