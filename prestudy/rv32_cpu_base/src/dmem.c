#include <stddef.h>

#include "rv32/dmem.h"

static rv32_status_t check_access(
    const rv32_dmem_t *dmem,
    uint32_t address)
{
    if (dmem == NULL || dmem->data == NULL) {
        return RV32_ERR_BAD_ARGUMENT;
    }
    if ((address & 0x3u) != 0u ||
        dmem->size < sizeof(uint32_t) ||
        address > dmem->size - sizeof(uint32_t)) {
        return RV32_ERR_MEMORY;
    }
    return RV32_OK;
}

rv32_status_t rv32_dmem_read_u32(
    const rv32_dmem_t *dmem,
    uint32_t address,
    uint32_t *value)
{
    rv32_status_t status;

    if (value == NULL) {
        return RV32_ERR_BAD_ARGUMENT;
    }
    status = check_access(dmem, address);
    if (status != RV32_OK) {
        return status;
    }

    *value =
        (uint32_t)dmem->data[address] |
        ((uint32_t)dmem->data[address + 1u] << 8u) |
        ((uint32_t)dmem->data[address + 2u] << 16u) |
        ((uint32_t)dmem->data[address + 3u] << 24u);
    return RV32_OK;
}

rv32_status_t rv32_dmem_write_u32(
    rv32_dmem_t *dmem,
    uint32_t address,
    uint32_t value)
{
    rv32_status_t status = check_access(dmem, address);

    if (status != RV32_OK) {
        return status;
    }

    dmem->data[address] = (uint8_t)(value & 0xffu);
    dmem->data[address + 1u] = (uint8_t)((value >> 8u) & 0xffu);
    dmem->data[address + 2u] = (uint8_t)((value >> 16u) & 0xffu);
    dmem->data[address + 3u] = (uint8_t)((value >> 24u) & 0xffu);
    return RV32_OK;
}
