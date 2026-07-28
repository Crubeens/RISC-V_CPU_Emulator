#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>
#include <utility>

#include "rv32/core/decode.hpp"

namespace {

using Kind = rv32::InstructionKind;

int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

struct DecodeCase {
    std::string_view name;
    std::uint16_t raw{};
    Kind kind{Kind::Illegal};
    std::uint32_t rd{};
    std::uint32_t rs1{};
    std::uint32_t rs2{};
    std::uint32_t immediate{};
};

void check_decode(const DecodeCase& expected)
{
    const auto actual =
        rv32::decode_compressed_instruction(expected.raw);
    if (actual.kind == expected.kind &&
        actual.raw == expected.raw &&
        actual.rd == expected.rd &&
        actual.rs1 == expected.rs1 &&
        actual.rs2 == expected.rs2 &&
        actual.immediate == expected.immediate &&
        !actual.acquire &&
        !actual.release &&
        actual.csr == 0U &&
        actual.length == 2U &&
        actual.valid()) {
        return;
    }

    std::cerr
        << "FAIL compressed decode " << expected.name
        << " raw=0x" << std::hex << std::setw(4)
        << std::setfill('0') << expected.raw
        << " actual={kind=" << std::dec
        << static_cast<unsigned int>(actual.kind)
        << ", rd=" << actual.rd
        << ", rs1=" << actual.rs1
        << ", rs2=" << actual.rs2
        << ", imm=0x" << std::hex << actual.immediate
        << ", length=" << std::dec
        << static_cast<unsigned int>(actual.length)
        << "} expected={kind="
        << static_cast<unsigned int>(expected.kind)
        << ", rd=" << expected.rd
        << ", rs1=" << expected.rs1
        << ", rs2=" << expected.rs2
        << ", imm=0x" << std::hex << expected.immediate
        << "}\n" << std::setfill(' ') << std::dec;
    ++failures;
}

void check_illegal(std::string_view name, std::uint16_t raw)
{
    const auto decoded = rv32::decode_compressed_instruction(raw);
    if (decoded.kind == Kind::Illegal &&
        decoded.raw == raw &&
        decoded.rd == 0U &&
        decoded.rs1 == 0U &&
        decoded.rs2 == 0U &&
        decoded.immediate == 0U &&
        decoded.length == 2U &&
        !decoded.valid()) {
        return;
    }

    std::cerr
        << "FAIL reserved compressed encoding " << name
        << " raw=0x" << std::hex << raw << std::dec << '\n';
    ++failures;
}

void test_integer_encodings()
{
    // These fixed encodings are emitted by GNU binutils for RV32IMAC. They
    // exercise every integer instruction format in the C extension.
    constexpr std::array cases{
        DecodeCase{"C.ADDI4SPN", 0x0058U, Kind::Addi,
                   14U, 2U, 0U, 4U},
        DecodeCase{"C.LW", 0x431CU, Kind::Lw,
                   15U, 14U, 0U, 0U},
        DecodeCase{"C.SW", 0xC310U, Kind::Sw,
                   0U, 14U, 12U, 0U},
        DecodeCase{"C.NOP", 0x0001U, Kind::Addi,
                   0U, 0U, 0U, 0U},
        DecodeCase{"C.ADDI", 0x147DU, Kind::Addi,
                   8U, 8U, 0U, 0xFFFFFFFFU},
        DecodeCase{"C.JAL", 0x2831U, Kind::Jal,
                   1U, 0U, 0U, 28U},
        DecodeCase{"C.LI", 0x4415U, Kind::Addi,
                   8U, 0U, 0U, 5U},
        DecodeCase{"C.LUI", 0x6405U, Kind::Lui,
                   8U, 0U, 0U, 0x1000U},
        DecodeCase{"C.ADDI16SP", 0x717DU, Kind::Addi,
                   2U, 2U, 0U, 0xFFFFFFF0U},
        DecodeCase{"C.SRLI", 0x8005U, Kind::Srli,
                   8U, 8U, 0U, 1U},
        DecodeCase{"C.SRAI", 0x8405U, Kind::Srai,
                   8U, 8U, 0U, 1U},
        DecodeCase{"C.ANDI", 0x880DU, Kind::Andi,
                   8U, 8U, 0U, 3U},
        DecodeCase{"C.SUB", 0x8D85U, Kind::Sub,
                   11U, 11U, 9U, 0U},
        DecodeCase{"C.XOR", 0x8DA5U, Kind::Xor,
                   11U, 11U, 9U, 0U},
        DecodeCase{"C.OR", 0x8DC5U, Kind::Or,
                   11U, 11U, 9U, 0U},
        DecodeCase{"C.AND", 0x8DE5U, Kind::And,
                   11U, 11U, 9U, 0U},
        DecodeCase{"C.J", 0xA025U, Kind::Jal,
                   0U, 0U, 0U, 40U},
        DecodeCase{"C.BEQZ", 0xC011U, Kind::Beq,
                   0U, 8U, 0U, 4U},
        DecodeCase{"C.BNEZ", 0xE011U, Kind::Bne,
                   0U, 8U, 0U, 4U},
        DecodeCase{"C.SLLI", 0x040AU, Kind::Slli,
                   8U, 8U, 0U, 2U},
        DecodeCase{"C.LWSP", 0x4682U, Kind::Lw,
                   13U, 2U, 0U, 0U},
        DecodeCase{"C.JR", 0x8282U, Kind::Jalr,
                   0U, 5U, 0U, 0U},
        DecodeCase{"C.MV", 0x85A6U, Kind::Add,
                   11U, 0U, 9U, 0U},
        DecodeCase{"C.EBREAK", 0x9002U, Kind::Ebreak,
                   0U, 0U, 0U, 0U},
        DecodeCase{"C.JALR", 0x9282U, Kind::Jalr,
                   1U, 5U, 0U, 0U},
        DecodeCase{"C.ADD", 0x95A6U, Kind::Add,
                   11U, 11U, 9U, 0U},
        DecodeCase{"C.SWSP", 0xC032U, Kind::Sw,
                   0U, 2U, 12U, 0U},
    };

    for (const auto& test_case : cases) {
        check_decode(test_case);
    }
}

void test_immediate_boundaries_and_hints()
{
    // Largest CIW offset: C.ADDI4SPN x15, sp, 1020.
    check_decode(
        {"C.ADDI4SPN maximum", 0x1FFCU, Kind::Addi,
         15U, 2U, 0U, 1020U});

    // The sign bit alone gives the most-negative CJ/CB/CI16SP values.
    check_decode(
        {"C.J minimum", 0xB001U, Kind::Jal,
         0U, 0U, 0U, 0xFFFFF800U});
    check_decode(
        {"C.BEQZ minimum", 0xD001U, Kind::Beq,
         0U, 8U, 0U, 0xFFFFFF00U});
    check_decode(
        {"C.ADDI16SP minimum", 0x7101U, Kind::Addi,
         2U, 2U, 0U, 0xFFFFFE00U});

    // Architecturally defined HINT encodings behave as side-effect-free base
    // operations. Keeping them legal is required for binary compatibility.
    check_decode(
        {"C.ADDI hint", 0x0081U, Kind::Addi,
         1U, 1U, 0U, 0U});
    check_decode(
        {"C.LI hint", 0x4005U, Kind::Addi,
         0U, 0U, 0U, 1U});
    check_decode(
        {"C.MV hint", 0x8026U, Kind::Add,
         0U, 0U, 9U, 0U});
    check_decode(
        {"C.ADD hint", 0x9026U, Kind::Add,
         0U, 0U, 9U, 0U});
}

void test_reserved_and_unsupported_encodings()
{
    constexpr std::array cases{
        std::pair{"C.ADDI4SPN zero immediate", std::uint16_t{0x0000U}},
        std::pair{"C.ADDI16SP zero immediate", std::uint16_t{0x6101U}},
        std::pair{"C.LUI zero immediate", std::uint16_t{0x6181U}},
        std::pair{"RV32 C.SRLI shamt[5]", std::uint16_t{0x9005U}},
        std::pair{"RV64 C.SUBW", std::uint16_t{0x9C01U}},
        std::pair{"RV32 C.SLLI shamt[5]", std::uint16_t{0x140AU}},
        std::pair{"C.LWSP rd=x0", std::uint16_t{0x4002U}},
        std::pair{"C.JR rs1=x0", std::uint16_t{0x8002U}},
        std::pair{"not a compressed quadrant", std::uint16_t{0x0003U}},
        std::pair{"C.FLD", std::uint16_t{0x2000U}},
        std::pair{"C.FLW", std::uint16_t{0x6000U}},
        std::pair{"reserved quadrant-zero encoding", std::uint16_t{0x8000U}},
        std::pair{"C.FSD", std::uint16_t{0xA000U}},
        std::pair{"C.FSW", std::uint16_t{0xE000U}},
        std::pair{"C.FLDSP", std::uint16_t{0x2002U}},
        std::pair{"C.FLWSP", std::uint16_t{0x6002U}},
        std::pair{"C.FSDSP", std::uint16_t{0xA002U}},
        std::pair{"C.FSWSP", std::uint16_t{0xE002U}},
    };

    for (const auto& [name, raw] : cases) {
        check_illegal(name, raw);
    }
}

} // namespace

int main()
{
    test_integer_encodings();
    test_immediate_boundaries_and_hints();
    test_reserved_and_unsupported_encodings();

    if (failures == 0) {
        std::cout << "All RV32C decode tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
