/*
 * 存储器测试：
 * 检查 imem/dmem 的小端读写、相互独立性、地址对齐、
 * 越界访问和空指针错误。
 */

#include <stdint.h>
#include <stdio.h>

#include "rv32/dmem.h"
#include "rv32/imem.h"

static int failures;

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                   \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                              \
            ++failures;                                                       \
        }                                                                     \
    } while (0)

int main(void)
{
    uint8_t imem_data[16] = {0};
    uint8_t dmem_data[16] = {0};
    rv32_imem_t imem = {imem_data, sizeof(imem_data)};
    rv32_dmem_t dmem = {dmem_data, sizeof(dmem_data)};
    uint32_t value = 0u;

    CHECK(rv32_imem_write_u32(&imem, 4u, 0x12345678u) == RV32_OK);
    CHECK(rv32_imem_read_u32(&imem, 4u, &value) == RV32_OK);
    CHECK(value == 0x12345678u);
    CHECK(imem_data[4] == 0x78u);
    CHECK(imem_data[7] == 0x12u);

    CHECK(rv32_dmem_write_u32(&dmem, 4u, 0xaabbccddu) == RV32_OK);
    CHECK(rv32_dmem_read_u32(&dmem, 4u, &value) == RV32_OK);
    CHECK(value == 0xaabbccddu);

    /* 两块存储器地址相同，但内容必须互不影响。 */
    CHECK(rv32_imem_read_u32(&imem, 4u, &value) == RV32_OK);
    CHECK(value == 0x12345678u);

    CHECK(rv32_imem_read_u32(&imem, 2u, &value) == RV32_ERR_MEMORY);
    CHECK(rv32_dmem_write_u32(&dmem, 14u, 0u) == RV32_ERR_MEMORY);
    CHECK(rv32_imem_read_u32(&imem, 16u, &value) == RV32_ERR_MEMORY);
    CHECK(rv32_dmem_read_u32(NULL, 0u, &value) ==
          RV32_ERR_BAD_ARGUMENT);
    CHECK(rv32_imem_read_u32(&imem, 0u, NULL) ==
          RV32_ERR_BAD_ARGUMENT);

    if (failures == 0) {
        puts("test_memory passed");
    }
    return failures == 0 ? 0 : 1;
}
