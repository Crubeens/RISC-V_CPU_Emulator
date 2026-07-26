#ifndef RV32_DECODE_H
#define RV32_DECODE_H

#include <stdint.h>

#include "rv32/types.h"

rv32_status_t rv32_decode(uint32_t raw, rv32_decoded_t *decoded);

#endif
