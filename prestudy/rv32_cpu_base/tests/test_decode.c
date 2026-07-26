#include <stdint.h>
#include <stdio.h>

#include "rv32/decode.h"

static int failures;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                              \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

static uint32_t encode_i(
    int32_t imm,
    uint8_t rs1,
    uint8_t funct3,
    uint8_t rd,
    uint8_t opcode)
{
    return (((uint32_t)imm & 0xfffu) << 20u) |
           ((uint32_t)rs1 << 15u) |
           ((uint32_t)funct3 << 12u) |
           ((uint32_t)rd << 7u) |
           opcode;
}

static uint32_t encode_s(
    int32_t imm,
    uint8_t rs2,
    uint8_t rs1,
    uint8_t funct3)
{
    uint32_t value = (uint32_t)imm & 0xfffu;
    return ((value >> 5u) << 25u) |
           ((uint32_t)rs2 << 20u) |
           ((uint32_t)rs1 << 15u) |
           ((uint32_t)funct3 << 12u) |
           ((value & 0x1fu) << 7u) |
           0x23u;
}

static uint32_t encode_b(
    int32_t imm,
    uint8_t rs2,
    uint8_t rs1,
    uint8_t funct3)
{
    uint32_t value = (uint32_t)imm & 0x1fffu;
    return (((value >> 12u) & 0x1u) << 31u) |
           (((value >> 5u) & 0x3fu) << 25u) |
           ((uint32_t)rs2 << 20u) |
           ((uint32_t)rs1 << 15u) |
           ((uint32_t)funct3 << 12u) |
           (((value >> 1u) & 0xfu) << 8u) |
           (((value >> 11u) & 0x1u) << 7u) |
           0x63u;
}

int main(void)
{
    rv32_decoded_t decoded;

    CHECK(rv32_decode(
              encode_i(-1, 2u, 0u, 1u, 0x13u),
              &decoded) == RV32_OK);
    CHECK(decoded.kind == RV32_INST_ADDI);
    CHECK(decoded.rd == 1u);
    CHECK(decoded.rs1 == 2u);
    CHECK(decoded.imm == -1);

    CHECK(rv32_decode(
              encode_i(-4, 1u, 2u, 3u, 0x03u),
              &decoded) == RV32_OK);
    CHECK(decoded.kind == RV32_INST_LW);
    CHECK(decoded.rd == 3u);
    CHECK(decoded.rs1 == 1u);
    CHECK(decoded.imm == -4);

    CHECK(rv32_decode(encode_s(-8, 2u, 1u, 2u), &decoded) == RV32_OK);
    CHECK(decoded.kind == RV32_INST_SW);
    CHECK(decoded.rs1 == 1u);
    CHECK(decoded.rs2 == 2u);
    CHECK(decoded.imm == -8);

    CHECK(rv32_decode(encode_b(-4, 3u, 2u, 0u), &decoded) == RV32_OK);
    CHECK(decoded.kind == RV32_INST_BEQ);
    CHECK(decoded.rs1 == 2u);
    CHECK(decoded.rs2 == 3u);
    CHECK(decoded.imm == -4);

    CHECK(rv32_decode(0xffffffffu, &decoded) ==
          RV32_ERR_ILLEGAL_INSTRUCTION);
    CHECK(decoded.kind == RV32_INST_INVALID);
    CHECK(rv32_decode(0u, NULL) == RV32_ERR_BAD_ARGUMENT);

    if (failures == 0) {
        puts("test_decode passed");
    }
    return failures == 0 ? 0 : 1;
}
