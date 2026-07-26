#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

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

[[nodiscard]] constexpr std::uint32_t encode_r(
    std::uint32_t funct7,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd,
    std::uint32_t opcode) noexcept
{
    return ((funct7 & 0x7FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_i(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd,
    std::uint32_t opcode) noexcept
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_a(
    std::uint32_t funct5,
    bool acquire,
    bool release,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t rd) noexcept
{
    const std::uint32_t funct7 =
        ((funct5 & 0x1FU) << 2U) |
        (static_cast<std::uint32_t>(acquire) << 1U) |
        static_cast<std::uint32_t>(release);
    return encode_r(funct7, rs2, rs1, 0x2U, rd, 0x2FU);
}

[[nodiscard]] constexpr std::uint32_t encode_s(
    std::uint32_t immediate,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t opcode) noexcept
{
    return (((immediate >> 5U) & 0x7FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((immediate & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_b(
    std::uint32_t immediate,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t opcode) noexcept
{
    return (((immediate >> 12U) & 0x1U) << 31U) |
           (((immediate >> 5U) & 0x3FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           (((immediate >> 1U) & 0xFU) << 8U) |
           (((immediate >> 11U) & 0x1U) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t immediate,
    std::uint32_t rd,
    std::uint32_t opcode) noexcept
{
    return (immediate & 0xFFFFF000U) |
           ((rd & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_j(
    std::uint32_t immediate,
    std::uint32_t rd,
    std::uint32_t opcode) noexcept
{
    return (((immediate >> 20U) & 0x1U) << 31U) |
           (((immediate >> 1U) & 0x3FFU) << 21U) |
           (((immediate >> 11U) & 0x1U) << 20U) |
           (((immediate >> 12U) & 0xFFU) << 12U) |
           ((rd & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

struct DecodeCase {
    std::string_view name;
    std::uint32_t raw{};
    Kind kind{Kind::Illegal};
    std::uint32_t rd{};
    std::uint32_t rs1{};
    std::uint32_t rs2{};
    std::uint32_t immediate{};
    bool acquire{};
    bool release{};
    std::uint16_t csr{};
};

void check_decode(const DecodeCase& expected)
{
    const auto actual = rv32::decode_instruction(expected.raw);
    if (actual.kind == expected.kind &&
        actual.raw == expected.raw &&
        actual.rd == expected.rd &&
        actual.rs1 == expected.rs1 &&
        actual.rs2 == expected.rs2 &&
        actual.immediate == expected.immediate &&
        actual.acquire == expected.acquire &&
        actual.release == expected.release &&
        actual.csr == expected.csr &&
        actual.valid()) {
        return;
    }

    std::cerr
        << "FAIL decode " << expected.name
        << " raw=0x" << std::hex << std::setw(8)
        << std::setfill('0') << expected.raw
        << " actual={kind=" << std::dec
        << static_cast<unsigned int>(actual.kind)
        << ", rd=" << actual.rd
        << ", rs1=" << actual.rs1
        << ", rs2=" << actual.rs2
        << ", imm=0x" << std::hex << actual.immediate
        << ", aq=" << actual.acquire
        << ", rl=" << actual.release
        << ", csr=0x" << std::hex << actual.csr
        << "} expected={kind=" << std::dec
        << static_cast<unsigned int>(expected.kind)
        << ", rd=" << expected.rd
        << ", rs1=" << expected.rs1
        << ", rs2=" << expected.rs2
        << ", imm=0x" << std::hex << expected.immediate
        << ", aq=" << expected.acquire
        << ", rl=" << expected.release
        << ", csr=0x" << std::hex << expected.csr
        << "}\n" << std::setfill(' ') << std::dec;
    ++failures;
}

void check_illegal(std::string_view name, std::uint32_t raw)
{
    const auto decoded = rv32::decode_instruction(raw);
    if (decoded.kind == Kind::Illegal &&
        decoded.raw == raw &&
        decoded.rd == 0U &&
        decoded.rs1 == 0U &&
        decoded.rs2 == 0U &&
        decoded.immediate == 0U &&
        !decoded.acquire &&
        !decoded.release &&
        decoded.csr == 0U &&
        !decoded.valid()) {
        return;
    }

    std::cerr << "FAIL illegal encoding " << name
              << " raw=0x" << std::hex << raw << std::dec << '\n';
    ++failures;
}

void test_extract_fields()
{
    constexpr std::uint32_t r_instruction =
        encode_r(0x20U, 7U, 6U, 0U, 5U, 0x33U);
    const auto r_fields = rv32::extract_fields(r_instruction);

    CHECK(r_fields.opcode == 0x33U);
    CHECK(r_fields.rd == 5U);
    CHECK(r_fields.funct3 == 0U);
    CHECK(r_fields.rs1 == 6U);
    CHECK(r_fields.rs2 == 7U);
    CHECK(r_fields.funct7 == 0x20U);

    // Field extraction is mechanical. For an I-type instruction, the raw
    // rs2/funct7 positions overlap the immediate and must not be zeroed.
    const auto i_fields =
        rv32::extract_fields(0xFFF00113U);
    CHECK(i_fields.opcode == 0x13U);
    CHECK(i_fields.rd == 2U);
    CHECK(i_fields.funct3 == 0U);
    CHECK(i_fields.rs1 == 0U);
    CHECK(i_fields.rs2 == 0x1FU);
    CHECK(i_fields.funct7 == 0x7FU);

    const auto unknown_fields =
        rv32::extract_fields(0xFFFFFFFFU);
    CHECK(unknown_fields.opcode == 0x7FU);
    CHECK(unknown_fields.rd == 0x1FU);
    CHECK(unknown_fields.funct3 == 0x7U);
    CHECK(unknown_fields.rs1 == 0x1FU);
    CHECK(unknown_fields.rs2 == 0x1FU);
    CHECK(unknown_fields.funct7 == 0x7FU);
}

void test_legal_instructions()
{
    const DecodeCase cases[] = {
        {"LUI", encode_u(0x12345000U, 3U, 0x37U),
         Kind::Lui, 3U, 0U, 0U, 0x12345000U},
        {"AUIPC", encode_u(0x80000000U, 4U, 0x17U),
         Kind::Auipc, 4U, 0U, 0U, 0x80000000U},
        {"JAL", encode_j(8U, 1U, 0x6FU),
         Kind::Jal, 1U, 0U, 0U, 8U},
        {"JALR", encode_i(0xFFCU, 2U, 0U, 1U, 0x67U),
         Kind::Jalr, 1U, 2U, 0U, 0xFFFFFFFCU},

        {"BEQ", encode_b(16U, 2U, 1U, 0U, 0x63U),
         Kind::Beq, 0U, 1U, 2U, 16U},
        {"BNE", encode_b(16U, 2U, 1U, 1U, 0x63U),
         Kind::Bne, 0U, 1U, 2U, 16U},
        {"BLT", encode_b(16U, 2U, 1U, 4U, 0x63U),
         Kind::Blt, 0U, 1U, 2U, 16U},
        {"BGE", encode_b(16U, 2U, 1U, 5U, 0x63U),
         Kind::Bge, 0U, 1U, 2U, 16U},
        {"BLTU", encode_b(16U, 2U, 1U, 6U, 0x63U),
         Kind::Bltu, 0U, 1U, 2U, 16U},
        {"BGEU", encode_b(16U, 2U, 1U, 7U, 0x63U),
         Kind::Bgeu, 0U, 1U, 2U, 16U},

        {"LB", encode_i(0xFFCU, 2U, 0U, 3U, 0x03U),
         Kind::Lb, 3U, 2U, 0U, 0xFFFFFFFCU},
        {"LH", encode_i(0xFFCU, 2U, 1U, 3U, 0x03U),
         Kind::Lh, 3U, 2U, 0U, 0xFFFFFFFCU},
        {"LW", encode_i(0xFFCU, 2U, 2U, 3U, 0x03U),
         Kind::Lw, 3U, 2U, 0U, 0xFFFFFFFCU},
        {"LBU", encode_i(0xFFCU, 2U, 4U, 3U, 0x03U),
         Kind::Lbu, 3U, 2U, 0U, 0xFFFFFFFCU},
        {"LHU", encode_i(0xFFCU, 2U, 5U, 3U, 0x03U),
         Kind::Lhu, 3U, 2U, 0U, 0xFFFFFFFCU},

        {"SB", encode_s(0xFF8U, 3U, 2U, 0U, 0x23U),
         Kind::Sb, 0U, 2U, 3U, 0xFFFFFFF8U},
        {"SH", encode_s(0xFF8U, 3U, 2U, 1U, 0x23U),
         Kind::Sh, 0U, 2U, 3U, 0xFFFFFFF8U},
        {"SW", encode_s(0xFF8U, 3U, 2U, 2U, 0x23U),
         Kind::Sw, 0U, 2U, 3U, 0xFFFFFFF8U},

        {"ADDI", encode_i(5U, 2U, 0U, 3U, 0x13U),
         Kind::Addi, 3U, 2U, 0U, 5U},
        {"SLTI", encode_i(5U, 2U, 2U, 3U, 0x13U),
         Kind::Slti, 3U, 2U, 0U, 5U},
        {"SLTIU", encode_i(5U, 2U, 3U, 3U, 0x13U),
         Kind::Sltiu, 3U, 2U, 0U, 5U},
        {"XORI", encode_i(5U, 2U, 4U, 3U, 0x13U),
         Kind::Xori, 3U, 2U, 0U, 5U},
        {"ORI", encode_i(5U, 2U, 6U, 3U, 0x13U),
         Kind::Ori, 3U, 2U, 0U, 5U},
        {"ANDI", encode_i(5U, 2U, 7U, 3U, 0x13U),
         Kind::Andi, 3U, 2U, 0U, 5U},
        {"SLLI", encode_i(3U, 2U, 1U, 3U, 0x13U),
         Kind::Slli, 3U, 2U, 0U, 3U},
        {"SRLI", encode_i(4U, 2U, 5U, 3U, 0x13U),
         Kind::Srli, 3U, 2U, 0U, 4U},
        {"SRAI", encode_i(0x404U, 2U, 5U, 3U, 0x13U),
         Kind::Srai, 3U, 2U, 0U, 4U},

        {"ADD", encode_r(0U, 3U, 2U, 0U, 1U, 0x33U),
         Kind::Add, 1U, 2U, 3U, 0U},
        {"SUB", encode_r(0x20U, 3U, 2U, 0U, 1U, 0x33U),
         Kind::Sub, 1U, 2U, 3U, 0U},
        {"SLL", encode_r(0U, 3U, 2U, 1U, 1U, 0x33U),
         Kind::Sll, 1U, 2U, 3U, 0U},
        {"SLT", encode_r(0U, 3U, 2U, 2U, 1U, 0x33U),
         Kind::Slt, 1U, 2U, 3U, 0U},
        {"SLTU", encode_r(0U, 3U, 2U, 3U, 1U, 0x33U),
         Kind::Sltu, 1U, 2U, 3U, 0U},
        {"XOR", encode_r(0U, 3U, 2U, 4U, 1U, 0x33U),
         Kind::Xor, 1U, 2U, 3U, 0U},
        {"SRL", encode_r(0U, 3U, 2U, 5U, 1U, 0x33U),
         Kind::Srl, 1U, 2U, 3U, 0U},
        {"SRA", encode_r(0x20U, 3U, 2U, 5U, 1U, 0x33U),
         Kind::Sra, 1U, 2U, 3U, 0U},
        {"OR", encode_r(0U, 3U, 2U, 6U, 1U, 0x33U),
         Kind::Or, 1U, 2U, 3U, 0U},
        {"AND", encode_r(0U, 3U, 2U, 7U, 1U, 0x33U),
         Kind::And, 1U, 2U, 3U, 0U},

        {"MUL", encode_r(1U, 3U, 2U, 0U, 1U, 0x33U),
         Kind::Mul, 1U, 2U, 3U, 0U},
        {"MULH", encode_r(1U, 3U, 2U, 1U, 1U, 0x33U),
         Kind::Mulh, 1U, 2U, 3U, 0U},
        {"MULHSU", encode_r(1U, 3U, 2U, 2U, 1U, 0x33U),
         Kind::Mulhsu, 1U, 2U, 3U, 0U},
        {"MULHU", encode_r(1U, 3U, 2U, 3U, 1U, 0x33U),
         Kind::Mulhu, 1U, 2U, 3U, 0U},
        {"DIV", encode_r(1U, 3U, 2U, 4U, 1U, 0x33U),
         Kind::Div, 1U, 2U, 3U, 0U},
        {"DIVU", encode_r(1U, 3U, 2U, 5U, 1U, 0x33U),
         Kind::Divu, 1U, 2U, 3U, 0U},
        {"REM", encode_r(1U, 3U, 2U, 6U, 1U, 0x33U),
         Kind::Rem, 1U, 2U, 3U, 0U},
        {"REMU", encode_r(1U, 3U, 2U, 7U, 1U, 0x33U),
         Kind::Remu, 1U, 2U, 3U, 0U},

        {"LR.W.AQ", encode_a(0x02U, true, false, 0U, 2U, 1U),
         Kind::LrW, 1U, 2U, 0U, 0U, true, false},
        {"SC.W.RL", encode_a(0x03U, false, true, 3U, 2U, 1U),
         Kind::ScW, 1U, 2U, 3U, 0U, false, true},
        {"AMOSWAP.W.AQRL",
         encode_a(0x01U, true, true, 3U, 2U, 1U),
         Kind::AmoSwapW, 1U, 2U, 3U, 0U, true, true},
        {"AMOADD.W", encode_a(0x00U, false, false, 3U, 2U, 1U),
         Kind::AmoAddW, 1U, 2U, 3U, 0U},
        {"AMOXOR.W", encode_a(0x04U, false, false, 3U, 2U, 1U),
         Kind::AmoXorW, 1U, 2U, 3U, 0U},
        {"AMOAND.W", encode_a(0x0CU, false, false, 3U, 2U, 1U),
         Kind::AmoAndW, 1U, 2U, 3U, 0U},
        {"AMOOR.W", encode_a(0x08U, false, false, 3U, 2U, 1U),
         Kind::AmoOrW, 1U, 2U, 3U, 0U},
        {"AMOMIN.W", encode_a(0x10U, false, false, 3U, 2U, 1U),
         Kind::AmoMinW, 1U, 2U, 3U, 0U},
        {"AMOMAX.W", encode_a(0x14U, false, false, 3U, 2U, 1U),
         Kind::AmoMaxW, 1U, 2U, 3U, 0U},
        {"AMOMINU.W", encode_a(0x18U, false, false, 3U, 2U, 1U),
         Kind::AmoMinuW, 1U, 2U, 3U, 0U},
        {"AMOMAXU.W", encode_a(0x1CU, false, false, 3U, 2U, 1U),
         Kind::AmoMaxuW, 1U, 2U, 3U, 0U},

        {"FENCE", encode_i(0U, 0U, 0U, 0U, 0x0FU),
         Kind::Fence, 0U, 0U, 0U, 0U},
        {"FENCE reserved fields",
         encode_i(0xFFFU, 31U, 0U, 31U, 0x0FU),
         Kind::Fence, 0U, 0U, 0U, 0U},
        {"FENCE.TSO", encode_i(0x833U, 0U, 0U, 0U, 0x0FU),
         Kind::Fence, 0U, 0U, 0U, 0U},
        {"FENCE.I", encode_i(0U, 0U, 1U, 0U, 0x0FU),
         Kind::FenceI, 0U, 0U, 0U, 0U},
        {"FENCE.I reserved fields",
         encode_i(0xFFFU, 31U, 1U, 31U, 0x0FU),
         Kind::FenceI, 0U, 0U, 0U, 0U},

        {"CSRRW", encode_i(0x340U, 2U, 1U, 3U, 0x73U),
         Kind::Csrrw, 3U, 2U, 0U, 0U, false, false, 0x340U},
        {"CSRRS", encode_i(0xC00U, 2U, 2U, 3U, 0x73U),
         Kind::Csrrs, 3U, 2U, 0U, 0U, false, false, 0xC00U},
        {"CSRRC", encode_i(0xC00U, 2U, 3U, 3U, 0x73U),
         Kind::Csrrc, 3U, 2U, 0U, 0U, false, false, 0xC00U},
        {"CSRRWI", encode_i(0x340U, 5U, 5U, 3U, 0x73U),
         Kind::Csrrwi, 3U, 0U, 0U, 5U, false, false, 0x340U},
        {"CSRRSI", encode_i(0xC00U, 5U, 6U, 3U, 0x73U),
         Kind::Csrrsi, 3U, 0U, 0U, 5U, false, false, 0xC00U},
        {"CSRRCI", encode_i(0xC00U, 5U, 7U, 3U, 0x73U),
         Kind::Csrrci, 3U, 0U, 0U, 5U, false, false, 0xC00U},

        {"ECALL", 0x00000073U,
         Kind::Ecall, 0U, 0U, 0U, 0U},
        {"EBREAK", 0x00100073U,
         Kind::Ebreak, 0U, 0U, 0U, 0U},
        {"MRET", 0x30200073U,
         Kind::Mret, 0U, 0U, 0U, 0U},
        {"SRET", 0x10200073U,
         Kind::Sret, 0U, 0U, 0U, 0U},
        {"WFI", 0x10500073U,
         Kind::Wfi, 0U, 0U, 0U, 0U},
    };

    for (const auto& test_case : cases) {
        check_decode(test_case);
    }
}

void test_illegal_instructions()
{
    check_illegal("unknown opcode", 0x00000000U);
    check_illegal(
        "JALR bad funct3",
        encode_i(0U, 1U, 1U, 1U, 0x67U));
    check_illegal(
        "branch bad funct3",
        encode_b(4U, 2U, 1U, 2U, 0x63U));
    check_illegal(
        "load bad funct3",
        encode_i(0U, 1U, 3U, 1U, 0x03U));
    check_illegal(
        "store bad funct3",
        encode_s(0U, 2U, 1U, 3U, 0x23U));
    check_illegal(
        "SLLI bad funct7",
        encode_i(0x020U, 1U, 1U, 1U, 0x13U));
    check_illegal(
        "right shift bad funct7",
        encode_i(0x200U, 1U, 5U, 1U, 0x13U));
    check_illegal(
        "unknown OP funct7",
        encode_r(2U, 2U, 1U, 0U, 1U, 0x33U));
    check_illegal(
        "alternate funct7 only valid for SUB and SRA",
        encode_r(0x20U, 2U, 1U, 1U, 1U, 0x33U));
    check_illegal(
        "LR.W requires rs2 to be x0",
        encode_a(0x02U, false, false, 2U, 1U, 1U));
    check_illegal(
        "RV32A doubleword width is illegal",
        encode_r(0x08U, 0U, 1U, 0x3U, 1U, 0x2FU));
    check_illegal(
        "unknown AMO funct5",
        encode_a(0x05U, false, false, 2U, 1U, 1U));
    check_illegal(
        "MISC-MEM bad funct3",
        encode_i(0U, 0U, 2U, 0U, 0x0FU));
    check_illegal(
        "SYSTEM bad funct3",
        encode_i(0U, 0U, 4U, 0U, 0x73U));
    check_illegal("ECALL with nonzero rd", 0x000000F3U);
    check_illegal("EBREAK with nonzero rs1", 0x00108073U);
    check_illegal("MRET with nonzero rd", 0x302000F3U);
    check_illegal("SRET with nonzero rs1", 0x10208073U);
    check_illegal("WFI with nonzero rd", 0x105000F3U);
    check_illegal("all bits set", 0xFFFFFFFFU);
}

} // namespace

int main()
{
    test_extract_fields();
    test_legal_instructions();
    test_illegal_instructions();

    if (failures == 0) {
        std::cout
            << "All RV32IMA+Zicsr+Zifencei decode tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
