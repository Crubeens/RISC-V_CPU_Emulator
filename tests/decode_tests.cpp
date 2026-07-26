#include <cstdint>
#include <iostream>

#include "rv32/core/decode.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

void test_get_bits()
{
    CHECK(rv32::get_bits(0x00500093U, 6, 0) == 0x13U);
    CHECK(rv32::get_bits(0x00500093U, 11, 7) == 1U);
    CHECK(rv32::get_bits(0x80000000U, 31, 31) == 1U);
    CHECK(rv32::get_bits(0x000000F0U, 7, 4) == 0xFU);
    CHECK(rv32::get_bits(0xDEADBEEFU, 31, 0) == 0xDEADBEEFU);
}

void test_sign_extend()
{
    CHECK(rv32::sign_extend(0x7FFU, 12) == 0x000007FFU);
    CHECK(rv32::sign_extend(0x800U, 12) == 0xFFFFF800U);
    CHECK(rv32::sign_extend(0xFFFU, 12) == 0xFFFFFFFFU);
    CHECK(rv32::sign_extend(0U, 1) == 0U);
    CHECK(rv32::sign_extend(1U, 1) == 0xFFFFFFFFU);
    CHECK(
        rv32::sign_extend(0xFFFF07FFU, 12) ==
        0x000007FFU);
    CHECK(
        rv32::sign_extend(0xFFFF0800U, 12) ==
        0xFFFFF800U);
    CHECK(
        rv32::sign_extend(0x80000000U, 32) ==
        0x80000000U);
}

void test_i_immediate()
{
    CHECK(rv32::decode_i_imm(0x00500093U) == 5U);
    CHECK(
        rv32::decode_i_imm(0xFFF00113U) ==
        0xFFFFFFFFU);
    CHECK(
        rv32::decode_i_imm(0x80000013U) ==
        0xFFFFF800U);
    CHECK(rv32::decode_i_imm(0x7FF00013U) == 0x7FFU);
}

void test_s_immediate()
{
    CHECK(
        rv32::decode_s_imm(0xFE532C23U) ==
        0xFFFFFFF8U);
    CHECK(
        rv32::decode_s_imm(0x80000023U) ==
        0xFFFFF800U);
    CHECK(rv32::decode_s_imm(0x7E000FA3U) == 0x7FFU);
}

void test_b_immediate()
{
    CHECK(rv32::decode_b_imm(0x00208863U) == 16U);
    CHECK(
        rv32::decode_b_imm(0xFE208EE3U) ==
        0xFFFFFFFCU);
    CHECK(
        rv32::decode_b_imm(0x80000063U) ==
        0xFFFFF000U);
    CHECK(rv32::decode_b_imm(0x7E000FE3U) == 0xFFEU);
}

void test_u_immediate()
{
    CHECK(
        rv32::decode_u_imm(0x123451B7U) ==
        0x12345000U);
    CHECK(
        rv32::decode_u_imm(0x80000037U) ==
        0x80000000U);
    CHECK(
        rv32::decode_u_imm(0xFFFFF037U) ==
        0xFFFFF000U);
}

void test_j_immediate()
{
    CHECK(rv32::decode_j_imm(0x008000EFU) == 8U);
    CHECK(
        rv32::decode_j_imm(0xFFDFF06FU) ==
        0xFFFFFFFCU);
    CHECK(
        rv32::decode_j_imm(0x8000006FU) ==
        0xFFF00000U);
    CHECK(
        rv32::decode_j_imm(0x7FFFF06FU) ==
        0x000FFFFEU);
}

} // namespace

int main()
{
    test_get_bits();
    test_sign_extend();
    test_i_immediate();
    test_s_immediate();
    test_b_immediate();
    test_u_immediate();
    test_j_immediate();

    if (failures == 0) {
        std::cout << "All RV32 decode foundation tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
