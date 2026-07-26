#ifndef RV32_DMEM_H
#define RV32_DMEM_H

#include <stdint.h>

#include "rv32/types.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
} rv32_dmem_t;

rv32_status_t rv32_dmem_read_u32(
    const rv32_dmem_t *dmem,
    uint32_t address,
    uint32_t *value);

rv32_status_t rv32_dmem_write_u32(
    rv32_dmem_t *dmem,
    uint32_t address,
    uint32_t value);

#endif
