#include <stddef.h>

#include "rv32/cpu.h"
#include "rv32/decode.h"
#include "rv32/execute.h"

/*
 * TODO（第 6 步完成纸面推演后）：
 * 1. 初始化寄存器、PC 和内存引用。
 * 2. 在 rv32_cpu_step 中组织：
 *    fetch -> decode -> read operands -> execute/memory/write-back
 *    -> commit PC -> protect x0。
 */

rv32_status_t rv32_cpu_init(
    rv32_cpu_t *cpu,
    rv32_imem_t *imem,
    rv32_dmem_t *dmem,
    uint32_t reset_pc)
{
    int i;

    if (cpu == NULL || imem == NULL || dmem == NULL) {
        return RV32_ERR_BAD_ARGUMENT;
    }

    cpu->pc = reset_pc;
    cpu->imem = imem;
    cpu->dmem = dmem;
    for (i = 0; i < 32; ++i) {
        cpu->regs[i] = 0;
    }
    return RV32_OK;
}

rv32_status_t rv32_cpu_step(rv32_cpu_t *cpu)
{
    // fetch
    uint32_t raw;
    rv32_status_t status;

    if (cpu == NULL || cpu->imem == NULL || cpu->dmem == NULL) {
        return RV32_ERR_BAD_ARGUMENT;
    }

    status = rv32_imem_read_u32(cpu->imem, cpu->pc, &raw);
    if (status != RV32_OK) {
        return status;
    }

    // decode
    rv32_decoded_t decoded;
    status = rv32_decode(raw, &decoded);
    if (status != RV32_OK) {
        return status;
    }

    // execute/write-back/memory
    uint32_t next_pc;
    status = rv32_execute(cpu, &decoded, &next_pc);
    if (status != RV32_OK) {
        return status;
    }

    // commit PC
    cpu->pc = next_pc;

    // protect x0
    cpu->regs[0] = 0;

    return RV32_OK;
}
