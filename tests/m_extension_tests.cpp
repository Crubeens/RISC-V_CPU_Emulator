#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "rv32/core/execute.hpp"

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

[[nodiscard]] constexpr std::uint32_t encode_m(
    std::uint32_t funct3,
    std::uint32_t rd = 7U,
    std::uint32_t rs1 = 5U,
    std::uint32_t rs2 = 6U) noexcept
{
    return (1U << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x33U;
}

struct MCase {
    std::string_view name;
    std::uint32_t funct3{};
    rv32::InstructionKind kind{rv32::InstructionKind::Illegal};
    std::uint32_t lhs{};
    std::uint32_t rhs{};
    std::uint32_t expected{};
};

constexpr std::array cases{
    MCase{
        "MUL normal",
        0U,
        rv32::InstructionKind::Mul,
        7U,
        9U,
        63U,
    },
    MCase{
        "MUL keeps low word on overflow",
        0U,
        rv32::InstructionKind::Mul,
        0xFFFFFFFFU,
        2U,
        0xFFFFFFFEU,
    },
    MCase{
        "MULH signed negative product",
        1U,
        rv32::InstructionKind::Mulh,
        0xFFFFFFFEU,
        3U,
        0xFFFFFFFFU,
    },
    MCase{
        "MULH minimum times minimum",
        1U,
        rv32::InstructionKind::Mulh,
        0x80000000U,
        0x80000000U,
        0x40000000U,
    },
    MCase{
        "MULHSU positive operands",
        2U,
        rv32::InstructionKind::Mulhsu,
        3U,
        7U,
        0U,
    },
    MCase{
        "MULHSU negative times unsigned maximum",
        2U,
        rv32::InstructionKind::Mulhsu,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
    },
    MCase{
        "MULHU product crosses word boundary",
        3U,
        rv32::InstructionKind::Mulhu,
        0x10000000U,
        0x10U,
        1U,
    },
    MCase{
        "MULHU unsigned maximums",
        3U,
        rv32::InstructionKind::Mulhu,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
        0xFFFFFFFEU,
    },
    MCase{
        "DIV normal",
        4U,
        rv32::InstructionKind::Div,
        20U,
        3U,
        6U,
    },
    MCase{
        "DIV rounds a negative quotient toward zero",
        4U,
        rv32::InstructionKind::Div,
        0xFFFFFFECU,
        3U,
        0xFFFFFFFAU,
    },
    MCase{
        "DIV by zero returns all ones",
        4U,
        rv32::InstructionKind::Div,
        0x12345678U,
        0U,
        0xFFFFFFFFU,
    },
    MCase{
        "DIV signed overflow returns dividend",
        4U,
        rv32::InstructionKind::Div,
        0x80000000U,
        0xFFFFFFFFU,
        0x80000000U,
    },
    MCase{
        "DIVU normal",
        5U,
        rv32::InstructionKind::Divu,
        20U,
        3U,
        6U,
    },
    MCase{
        "DIVU unsigned maximum",
        5U,
        rv32::InstructionKind::Divu,
        0xFFFFFFFFU,
        2U,
        0x7FFFFFFFU,
    },
    MCase{
        "DIVU by zero returns all ones",
        5U,
        rv32::InstructionKind::Divu,
        0x12345678U,
        0U,
        0xFFFFFFFFU,
    },
    MCase{
        "REM normal",
        6U,
        rv32::InstructionKind::Rem,
        20U,
        3U,
        2U,
    },
    MCase{
        "REM keeps dividend sign",
        6U,
        rv32::InstructionKind::Rem,
        0xFFFFFFECU,
        3U,
        0xFFFFFFFEU,
    },
    MCase{
        "REM by zero returns dividend",
        6U,
        rv32::InstructionKind::Rem,
        0x12345678U,
        0U,
        0x12345678U,
    },
    MCase{
        "REM signed overflow returns zero",
        6U,
        rv32::InstructionKind::Rem,
        0x80000000U,
        0xFFFFFFFFU,
        0U,
    },
    MCase{
        "REMU normal",
        7U,
        rv32::InstructionKind::Remu,
        20U,
        3U,
        2U,
    },
    MCase{
        "REMU unsigned maximum",
        7U,
        rv32::InstructionKind::Remu,
        0xFFFFFFFFU,
        2U,
        1U,
    },
    MCase{
        "REMU by zero returns dividend",
        7U,
        rv32::InstructionKind::Remu,
        0x89ABCDEFU,
        0U,
        0x89ABCDEFU,
    },
};

void test_decode_execute_and_commit()
{
    constexpr std::uint32_t pc = 0x80000000U;
    constexpr std::uint32_t rd = 7U;

    for (const auto& test : cases) {
        const std::uint32_t raw = encode_m(test.funct3);
        const auto decoded = rv32::decode_instruction(raw);
        const auto pending = rv32::execute_decoded(
            decoded,
            pc,
            test.lhs,
            test.rhs);

        rv32::CpuSnapshot state{};
        state.pc = pc;
        const bool committed = rv32::commit_pending(state, pending);

        const bool passed =
            decoded.kind == test.kind &&
            decoded.rd == rd &&
            decoded.rs1 == 5U &&
            decoded.rs2 == 6U &&
            pending.ready() &&
            pending.pc == pc &&
            pending.instruction == raw &&
            pending.next_pc == pc + 4U &&
            pending.register_write.enabled &&
            pending.register_write.index == rd &&
            pending.register_write.value == test.expected &&
            committed &&
            state.registers[rd] == test.expected &&
            state.pc == pc + 4U &&
            state.instructions_retired == 1U;

        if (!passed) {
            std::cerr
                << "FAIL M case \"" << test.name
                << "\": expected=0x" << std::hex << std::setw(8)
                << std::setfill('0') << test.expected
                << ", actual=0x" << std::setw(8)
                << pending.register_write.value
                << std::setfill(' ') << std::dec
                << ", decoded="
                << static_cast<unsigned int>(decoded.kind)
                << ", ready=" << pending.ready()
                << ", committed=" << committed
                << '\n';
            ++failures;
        }
    }
}

void test_m_write_to_x0_is_discarded()
{
    constexpr std::uint32_t pc = 0x80000000U;
    const auto decoded =
        rv32::decode_instruction(encode_m(0U, 0U));
    const auto pending =
        rv32::execute_decoded(decoded, pc, 6U, 7U);

    rv32::CpuSnapshot state{};
    state.pc = pc;
    state.registers[0] = 0xFFFFFFFFU;

    CHECK(rv32::commit_pending(state, pending));
    CHECK(state.registers[0] == 0U);
    CHECK(state.pc == pc + 4U);
    CHECK(state.instructions_retired == 1U);
}

} // namespace

int main()
{
    test_decode_execute_and_commit();
    test_m_write_to_x0_is_discarded();

    if (failures == 0) {
        std::cout << "All RV32M extension tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
