#ifndef RV32_IMEM_H
#define RV32_IMEM_H

#include <stdint.h>

#include "rv32/types.h"

typedef struct {
    uint8_t *data;
    uint32_t size;
} rv32_imem_t;

rv32_status_t rv32_imem_read_u32(
    const rv32_imem_t *imem,
    uint32_t address,
    uint32_t *value);

/* 仅供加载程序使用，CPU 执行指令时不会写 imem。 */
rv32_status_t rv32_imem_write_u32(
    rv32_imem_t *imem,
    uint32_t address,
    uint32_t value);

#endif
