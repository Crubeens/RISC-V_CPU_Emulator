#ifndef RV32_TYPES_H
#define RV32_TYPES_H

#include <stdint.h>

typedef enum {
    RV32_OK = 0,
    RV32_ERR_BAD_ARGUMENT,
    RV32_ERR_MEMORY,
    RV32_ERR_ILLEGAL_INSTRUCTION
} rv32_status_t;

typedef enum {
    RV32_INST_INVALID = 0,
    RV32_INST_ADDI,
    RV32_INST_LW,
    RV32_INST_SW,
    RV32_INST_BEQ
} rv32_inst_kind_t;

typedef struct {
    uint32_t raw;
    uint8_t opcode;
    uint8_t rd;
    uint8_t rs1;
    uint8_t rs2;
    uint8_t funct3;
    uint8_t funct7;
    int32_t imm;
    rv32_inst_kind_t kind;
} rv32_decoded_t;

#endif
