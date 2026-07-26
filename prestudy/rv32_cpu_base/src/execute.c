#include <stddef.h>

#include "rv32/dmem.h"
#include "rv32/execute.h"

rv32_status_t rv32_execute(
    rv32_cpu_t *cpu,
    const rv32_decoded_t *decoded,
    uint32_t *next_pc)
{
    uint32_t rs1_v;
    uint32_t rs2_v;
    uint32_t rd_v;
    uint32_t address;
    rv32_status_t status;

    if (cpu == NULL || decoded == NULL || next_pc == NULL ||
        cpu->dmem == NULL) {
        return RV32_ERR_BAD_ARGUMENT;
    }

    *next_pc = cpu->pc + 4u;

    switch (decoded->kind) {
        case RV32_INST_ADDI:
            rs1_v = cpu->regs[decoded->rs1];
            rd_v = rs1_v + (uint32_t)decoded->imm;
            cpu->regs[decoded->rd] = rd_v;
            break;
        case RV32_INST_LW:
            rs1_v = cpu->regs[decoded->rs1];
            address = rs1_v + (uint32_t)decoded->imm;
            status = rv32_dmem_read_u32(cpu->dmem, address, &rd_v);
            if (status != RV32_OK) {
                return status;
            }
            cpu->regs[decoded->rd] = rd_v;
            break;
        case RV32_INST_SW:
            rs1_v = cpu->regs[decoded->rs1];
            rs2_v = cpu->regs[decoded->rs2];
            address = rs1_v + (uint32_t)decoded->imm;
            status = rv32_dmem_write_u32(cpu->dmem, address, rs2_v);
            if (status != RV32_OK) {
                return status;
            }
            break;
        case RV32_INST_BEQ:
            rs1_v = cpu->regs[decoded->rs1];
            rs2_v = cpu->regs[decoded->rs2];
            if (rs1_v == rs2_v) {
                *next_pc = cpu->pc + (uint32_t)decoded->imm;
            }
            break;
        default:
            return RV32_ERR_ILLEGAL_INSTRUCTION;
    }
    return RV32_OK;
}
