#ifndef RV32_EXECUTE_H
#define RV32_EXECUTE_H

#include <stdint.h>

#include "rv32/cpu.h"
#include "rv32/types.h"

rv32_status_t rv32_execute(
    rv32_cpu_t *cpu,
    const rv32_decoded_t *decoded,
    uint32_t *next_pc);

#endif
