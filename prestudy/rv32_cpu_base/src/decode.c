#include <stddef.h>

#include "rv32/decode.h"

/*
 * TODO：
 * - 提取 opcode、rd、rs1、rs2、funct3、funct7。
 * - 分别拼接 I/S/B 型立即数并进行符号扩展。
 * - 只识别 ADDI、LW、SW、BEQ。
 */

static int32_t sign_extend(uint32_t value, uint8_t width)
{
    uint32_t sign_bit = 1u << (width - 1u);
    return (int32_t)((value ^ sign_bit) - sign_bit);
}

rv32_status_t rv32_decode(uint32_t raw, rv32_decoded_t *decoded)
{
    uint32_t opcode;
    uint32_t immediate;

    if (decoded == NULL) {
        return RV32_ERR_BAD_ARGUMENT;
    }

    *decoded = (rv32_decoded_t){0};
    opcode = raw & 0x7fu;
    decoded->raw = raw;
    decoded->opcode = (uint8_t)opcode;
    decoded->rd = (uint8_t)((raw >> 7u) & 0x1fu);
    decoded->funct3 = (uint8_t)((raw >> 12u) & 0x7u);
    decoded->rs1 = (uint8_t)((raw >> 15u) & 0x1fu);
    decoded->rs2 = (uint8_t)((raw >> 20u) & 0x1fu);
    decoded->funct7 = (uint8_t)((raw >> 25u) & 0x7fu);

    switch (opcode) {
        case 0x13u: // ADDI
            if (decoded->funct3 != 0x0u) {
                break;
            }
            decoded->imm = sign_extend(raw >> 20u, 12u);
            decoded->kind = RV32_INST_ADDI;
            return RV32_OK;
        case 0x03u: // LW
            if (decoded->funct3 != 0x2u) {
                break;
            }
            decoded->imm = sign_extend(raw >> 20u, 12u);
            decoded->kind = RV32_INST_LW;
            return RV32_OK;
        case 0x23u: // SW
            if (decoded->funct3 != 0x2u) {
                break;
            }
            immediate =
                ((raw >> 25u) << 5u) |
                ((raw >> 7u) & 0x1fu);
            decoded->imm = sign_extend(immediate, 12u);
            decoded->kind = RV32_INST_SW;
            return RV32_OK;
        case 0x63u: // BEQ
            if (decoded->funct3 != 0x0u) {
                break;
            }
            immediate =
                ((raw >> 31u) << 12u) |
                (((raw >> 7u) & 0x1u) << 11u) |
                (((raw >> 25u) & 0x3fu) << 5u) |
                (((raw >> 8u) & 0xfu) << 1u);
            decoded->imm = sign_extend(immediate, 13u);
            decoded->kind = RV32_INST_BEQ;
            return RV32_OK;
        default:
            break;
    }

    decoded->kind = RV32_INST_INVALID;
    return RV32_ERR_ILLEGAL_INSTRUCTION;
}
