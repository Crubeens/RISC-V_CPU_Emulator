#ifndef RV32_CPU_H
#define RV32_CPU_H

#include <stdint.h>

#include "rv32/dmem.h"
#include "rv32/imem.h"
#include "rv32/types.h"

typedef struct {
    uint32_t regs[32];
    uint32_t pc;
    rv32_imem_t *imem;
    rv32_dmem_t *dmem;
} rv32_cpu_t;

rv32_status_t rv32_cpu_init(
    rv32_cpu_t *cpu,
    rv32_imem_t *imem,
    rv32_dmem_t *dmem,
    uint32_t reset_pc);

rv32_status_t rv32_cpu_step(rv32_cpu_t *cpu);

#endif
