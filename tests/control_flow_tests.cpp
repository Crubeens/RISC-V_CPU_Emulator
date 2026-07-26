#include <array>
#include <cstdint>
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

[[nodiscard]] constexpr rv32::DecodedInstruction make_decoded(
    rv32::InstructionKind kind,
    std::uint32_t rd = 0,
    std::uint32_t immediate = 0) noexcept
{
    return {
        .kind = kind,
        .raw = 0xA5A5006FU,
        .rd = rd,
        .rs1 = 1U,
        .rs2 = 2U,
        .immediate = immediate,
    };
}

void test_auipc_uses_instruction_pc_and_wraps()
{
    const auto normal = rv32::execute_control_flow(
        make_decoded(
            rv32::InstructionKind::Auipc,
            3U,
            0x2000U),
        0x80000100U,
        0,
        0);

    CHECK(normal.ready());
    CHECK(normal.pending.next_pc == 0x80000104U);
    CHECK(normal.pending.register_write.enabled);
    CHECK(normal.pending.register_write.index == 3U);
    CHECK(normal.pending.register_write.value == 0x80002100U);

    const auto result = rv32::execute_control_flow(
        make_decoded(
            rv32::InstructionKind::Auipc,
            3U,
            0x1000U),
        0xFFFFFFFCU,
        0,
        0);

    CHECK(result.ready());
    CHECK(result.pending.ready());
    CHECK(result.pending.pc == 0xFFFFFFFCU);
    CHECK(result.pending.next_pc == 0U);
    CHECK(result.pending.register_write.enabled);
    CHECK(result.pending.register_write.index == 3U);
    CHECK(result.pending.register_write.value == 0x00000FFCU);
    CHECK(result.trap_value == 0U);
}

void test_jumps_write_link_and_validate_the_target()
{
    constexpr std::uint32_t pc = 0x80000100U;

    const auto jal = rv32::execute_control_flow(
        make_decoded(
            rv32::InstructionKind::Jal,
            5U,
            0xFFFFFFFCU),
        pc,
        0,
        0);
    CHECK(jal.ready());
    CHECK(jal.pending.next_pc == pc - 4U);
    CHECK(jal.pending.register_write.enabled);
    CHECK(jal.pending.register_write.index == 5U);
    CHECK(jal.pending.register_write.value == pc + 4U);

    const auto jalr = rv32::execute_control_flow(
        make_decoded(rv32::InstructionKind::Jalr, 6U, 0U),
        pc,
        0x80000201U,
        0);
    CHECK(jalr.ready());
    CHECK(jalr.pending.next_pc == 0x80000200U);
    CHECK(jalr.pending.register_write.enabled);
    CHECK(jalr.pending.register_write.index == 6U);
    CHECK(jalr.pending.register_write.value == pc + 4U);

    const auto bad_jal = rv32::execute_control_flow(
        make_decoded(rv32::InstructionKind::Jal, 7U, 2U),
        pc,
        0,
        0);
    CHECK(
        bad_jal.status ==
        rv32::ControlFlowStatus::InstructionAddressMisaligned);
    CHECK(!bad_jal.pending.ready());
    CHECK(!bad_jal.pending.register_write.enabled);
    CHECK(bad_jal.trap_value == pc + 2U);

    const auto bad_jalr = rv32::execute_control_flow(
        make_decoded(rv32::InstructionKind::Jalr, 7U, 0U),
        pc,
        0x80000203U,
        0);
    CHECK(
        bad_jalr.status ==
        rv32::ControlFlowStatus::InstructionAddressMisaligned);
    CHECK(!bad_jalr.pending.ready());
    CHECK(!bad_jalr.pending.register_write.enabled);
    CHECK(bad_jalr.trap_value == 0x80000202U);
}

struct BranchCase {
    std::string_view name;
    rv32::InstructionKind kind;
    std::uint32_t rs1;
    std::uint32_t rs2;
    bool taken;
};

constexpr std::array branch_cases{
    BranchCase{"beq taken", rv32::InstructionKind::Beq, 7U, 7U, true},
    BranchCase{"beq not taken", rv32::InstructionKind::Beq, 7U, 8U, false},
    BranchCase{"bne taken", rv32::InstructionKind::Bne, 7U, 8U, true},
    BranchCase{"bne not taken", rv32::InstructionKind::Bne, 7U, 7U, false},
    BranchCase{
        "blt signed taken",
        rv32::InstructionKind::Blt,
        0x80000000U,
        0U,
        true,
    },
    BranchCase{
        "blt signed not taken",
        rv32::InstructionKind::Blt,
        0U,
        0xFFFFFFFFU,
        false,
    },
    BranchCase{
        "bge signed taken",
        rv32::InstructionKind::Bge,
        0U,
        0xFFFFFFFFU,
        true,
    },
    BranchCase{
        "bge signed not taken",
        rv32::InstructionKind::Bge,
        0x80000000U,
        0U,
        false,
    },
    BranchCase{
        "bltu taken",
        rv32::InstructionKind::Bltu,
        0U,
        0xFFFFFFFFU,
        true,
    },
    BranchCase{
        "bltu not taken",
        rv32::InstructionKind::Bltu,
        0xFFFFFFFFU,
        0U,
        false,
    },
    BranchCase{
        "bgeu taken",
        rv32::InstructionKind::Bgeu,
        0xFFFFFFFFU,
        0U,
        true,
    },
    BranchCase{
        "bgeu not taken",
        rv32::InstructionKind::Bgeu,
        0U,
        0xFFFFFFFFU,
        false,
    },
};

void test_all_branch_conditions()
{
    constexpr std::uint32_t pc = 0x80000100U;

    for (const auto& test : branch_cases) {
        const auto result = rv32::execute_control_flow(
            make_decoded(test.kind, 0, 8U),
            pc,
            test.rs1,
            test.rs2);

        const std::uint32_t expected_pc =
            test.taken ? pc + 8U : pc + 4U;
        if (!result.ready() ||
            result.pending.next_pc != expected_pc ||
            result.pending.register_write.enabled) {
            std::cerr << "FAIL branch case \"" << test.name << "\"\n";
            ++failures;
        }
    }
}

void test_each_taken_branch_checks_alignment()
{
    constexpr std::uint32_t pc = 0x80000100U;
    struct TakenCase {
        rv32::InstructionKind kind;
        std::uint32_t rs1;
        std::uint32_t rs2;
    };
    constexpr std::array cases{
        TakenCase{rv32::InstructionKind::Beq, 9U, 9U},
        TakenCase{rv32::InstructionKind::Bne, 9U, 10U},
        TakenCase{rv32::InstructionKind::Blt, 0x80000000U, 0U},
        TakenCase{rv32::InstructionKind::Bge, 0U, 0xFFFFFFFFU},
        TakenCase{rv32::InstructionKind::Bltu, 0U, 0xFFFFFFFFU},
        TakenCase{rv32::InstructionKind::Bgeu, 0xFFFFFFFFU, 0U},
    };

    for (const auto& test : cases) {
        const auto taken = rv32::execute_control_flow(
            make_decoded(test.kind, 0, 2U),
            pc,
            test.rs1,
            test.rs2);
        CHECK(
            taken.status ==
            rv32::ControlFlowStatus::InstructionAddressMisaligned);
        CHECK(!taken.pending.ready());
        CHECK(taken.trap_value == pc + 2U);
    }

    const auto decoded =
        make_decoded(rv32::InstructionKind::Beq, 0, 2U);
    const auto not_taken =
        rv32::execute_control_flow(decoded, pc, 9U, 10U);
    CHECK(not_taken.ready());
    CHECK(not_taken.pending.next_pc == pc + 4U);
    CHECK(not_taken.trap_value == 0U);
}

void test_non_control_flow_instruction_is_rejected()
{
    const auto result = rv32::execute_control_flow(
        make_decoded(rv32::InstructionKind::Addi, 1U, 4U),
        0x80000000U,
        1U,
        2U);

    CHECK(
        result.status ==
        rv32::ControlFlowStatus::NotControlFlowInstruction);
    CHECK(!result.ready());
    CHECK(!result.pending.ready());
    CHECK(result.pending.next_pc == 0x80000000U);
    CHECK(!result.pending.register_write.enabled);
    CHECK(result.trap_value == 0U);
}

} // namespace

int main()
{
    test_auipc_uses_instruction_pc_and_wraps();
    test_jumps_write_link_and_validate_the_target();
    test_all_branch_conditions();
    test_each_taken_branch_checks_alignment();
    test_non_control_flow_instruction_is_rejected();

    if (failures == 0) {
        std::cout << "All RV32 control-flow tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
