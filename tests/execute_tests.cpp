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

struct AluCase {
    std::string_view name;
    rv32::InstructionKind kind;
    std::uint32_t rs1_value;
    std::uint32_t rs2_value;
    std::uint32_t immediate;
    std::uint32_t expected;
};

constexpr std::array alu_cases{
    AluCase{
        "lui",
        rv32::InstructionKind::Lui,
        0,
        0,
        0x12345000U,
        0x12345000U,
    },
    AluCase{
        "lui keeps the top immediate bits",
        rv32::InstructionKind::Lui,
        0,
        0,
        0xFFFFF000U,
        0xFFFFF000U,
    },
    AluCase{
        "addi normal",
        rv32::InstructionKind::Addi,
        10U,
        0,
        5U,
        15U,
    },
    AluCase{
        "addi negative immediate",
        rv32::InstructionKind::Addi,
        5U,
        0,
        0xFFFFFFFFU,
        4U,
    },
    AluCase{
        "addi wraps",
        rv32::InstructionKind::Addi,
        0xFFFFFFFFU,
        0,
        1U,
        0U,
    },
    AluCase{
        "slti normal",
        rv32::InstructionKind::Slti,
        3U,
        0,
        5U,
        1U,
    },
    AluCase{
        "slti signed boundary",
        rv32::InstructionKind::Slti,
        0x80000000U,
        0,
        0x7FFFFFFFU,
        1U,
    },
    AluCase{
        "sltiu normal",
        rv32::InstructionKind::Sltiu,
        3U,
        0,
        5U,
        1U,
    },
    AluCase{
        "sltiu sign-extended immediate boundary",
        rv32::InstructionKind::Sltiu,
        0xFFFFFFFFU,
        0,
        1U,
        0U,
    },
    AluCase{
        "xori",
        rv32::InstructionKind::Xori,
        0x0F0F0000U,
        0,
        0x00FF00FFU,
        0x0FF000FFU,
    },
    AluCase{
        "xori all-ones immediate",
        rv32::InstructionKind::Xori,
        0x0F0F0000U,
        0,
        0xFFFFFFFFU,
        0xF0F0FFFFU,
    },
    AluCase{
        "ori",
        rv32::InstructionKind::Ori,
        0x0F0F0000U,
        0,
        0x00FF00FFU,
        0x0FFF00FFU,
    },
    AluCase{
        "ori all-ones immediate",
        rv32::InstructionKind::Ori,
        0x0F0F0000U,
        0,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
    },
    AluCase{
        "andi",
        rv32::InstructionKind::Andi,
        0x0F0F0000U,
        0,
        0x00FF00FFU,
        0x000F0000U,
    },
    AluCase{
        "andi zero immediate",
        rv32::InstructionKind::Andi,
        0xFFFFFFFFU,
        0,
        0,
        0,
    },
    AluCase{
        "slli normal",
        rv32::InstructionKind::Slli,
        0x11U,
        0,
        4U,
        0x110U,
    },
    AluCase{
        "slli maximum shift",
        rv32::InstructionKind::Slli,
        1U,
        0,
        31U,
        0x80000000U,
    },
    AluCase{
        "srli normal",
        rv32::InstructionKind::Srli,
        0x80000000U,
        0,
        4U,
        0x08000000U,
    },
    AluCase{
        "srli maximum shift",
        rv32::InstructionKind::Srli,
        0x80000000U,
        0,
        31U,
        1U,
    },
    AluCase{
        "srai normal positive",
        rv32::InstructionKind::Srai,
        0x70000000U,
        0,
        4U,
        0x07000000U,
    },
    AluCase{
        "srai maximum shift preserves sign",
        rv32::InstructionKind::Srai,
        0x80000000U,
        0,
        31U,
        0xFFFFFFFFU,
    },
    AluCase{
        "add normal",
        rv32::InstructionKind::Add,
        7U,
        9U,
        0,
        16U,
    },
    AluCase{
        "add wraps",
        rv32::InstructionKind::Add,
        0xFFFFFFFFU,
        2U,
        0,
        1U,
    },
    AluCase{
        "sub normal",
        rv32::InstructionKind::Sub,
        9U,
        7U,
        0,
        2U,
    },
    AluCase{
        "sub wraps",
        rv32::InstructionKind::Sub,
        1U,
        2U,
        0,
        0xFFFFFFFFU,
    },
    AluCase{
        "sll normal",
        rv32::InstructionKind::Sll,
        0x11U,
        4U,
        0,
        0x110U,
    },
    AluCase{
        "sll masks rs2",
        rv32::InstructionKind::Sll,
        1U,
        33U,
        0,
        2U,
    },
    AluCase{
        "slt normal",
        rv32::InstructionKind::Slt,
        3U,
        5U,
        0,
        1U,
    },
    AluCase{
        "slt signed boundary",
        rv32::InstructionKind::Slt,
        0x80000000U,
        0xFFFFFFFFU,
        0,
        1U,
    },
    AluCase{
        "sltu normal",
        rv32::InstructionKind::Sltu,
        3U,
        5U,
        0,
        1U,
    },
    AluCase{
        "sltu unsigned boundary",
        rv32::InstructionKind::Sltu,
        0xFFFFFFFFU,
        1U,
        0,
        0U,
    },
    AluCase{
        "xor",
        rv32::InstructionKind::Xor,
        0xAAAA5555U,
        0x0F0FF0F0U,
        0,
        0xA5A5A5A5U,
    },
    AluCase{
        "xor equal operands",
        rv32::InstructionKind::Xor,
        0xA5A5A5A5U,
        0xA5A5A5A5U,
        0,
        0,
    },
    AluCase{
        "srl normal",
        rv32::InstructionKind::Srl,
        0x80000000U,
        4U,
        0,
        0x08000000U,
    },
    AluCase{
        "srl masks rs2",
        rv32::InstructionKind::Srl,
        0x80000000U,
        33U,
        0,
        0x40000000U,
    },
    AluCase{
        "sra normal positive",
        rv32::InstructionKind::Sra,
        0x70000000U,
        4U,
        0,
        0x07000000U,
    },
    AluCase{
        "sra negative preserves sign",
        rv32::InstructionKind::Sra,
        0x80000000U,
        4U,
        0,
        0xF8000000U,
    },
    AluCase{
        "or",
        rv32::InstructionKind::Or,
        0xA0005000U,
        0x0A00500AU,
        0,
        0xAA00500AU,
    },
    AluCase{
        "or with zero",
        rv32::InstructionKind::Or,
        0xA0005000U,
        0,
        0,
        0xA0005000U,
    },
    AluCase{
        "and",
        rv32::InstructionKind::And,
        0xAFF05FF0U,
        0x0FF00FF0U,
        0,
        0x0FF00FF0U,
    },
    AluCase{
        "and with zero",
        rv32::InstructionKind::And,
        0xAFF05FF0U,
        0,
        0,
        0,
    },
};

[[nodiscard]] bool same_snapshot(
    const rv32::CpuSnapshot& left,
    const rv32::CpuSnapshot& right)
{
    return left == right;
}

void test_integer_execution()
{
    constexpr std::uint32_t pc = 0x80000000U;
    constexpr std::uint32_t raw = 0xA5A50013U;
    constexpr std::uint32_t rd = 7U;

    for (const auto& test : alu_cases) {
        const rv32::DecodedInstruction decoded{
            .kind = test.kind,
            .raw = raw,
            .rd = rd,
            .rs1 = 1U,
            .rs2 = 2U,
            .immediate = test.immediate,
        };
        const auto pending = rv32::execute_decoded(
            decoded,
            pc,
            test.rs1_value,
            test.rs2_value);

        const bool passed =
            pending.ready() &&
            pending.pc == pc &&
            pending.instruction == raw &&
            pending.next_pc == pc + 4U &&
            pending.register_write.enabled &&
            pending.register_write.index == rd &&
            pending.register_write.value == test.expected;

        if (!passed) {
            std::cerr
                << "FAIL ALU case \"" << test.name
                << "\": expected 0x" << std::hex << std::setw(8)
                << std::setfill('0') << test.expected
                << ", got 0x" << std::setw(8)
                << pending.register_write.value
                << std::setfill(' ') << std::dec
                << ", ready=" << pending.ready()
                << ", write=" << pending.register_write.enabled
                << ", rd=" << pending.register_write.index
                << '\n';
            ++failures;
        }
    }
}

void test_next_pc_wraps()
{
    const rv32::DecodedInstruction decoded{
        .kind = rv32::InstructionKind::Addi,
        .raw = 0x00000013U,
        .rd = 1U,
        .rs1 = 0,
        .rs2 = 0,
        .immediate = 0,
    };

    const auto pending =
        rv32::execute_decoded(decoded, 0xFFFFFFFCU, 0, 0);

    CHECK(pending.ready());
    CHECK(pending.next_pc == 0U);
}

void test_unsupported_instruction_is_not_committable()
{
    const rv32::DecodedInstruction decoded{
        .kind = rv32::InstructionKind::Lw,
        .raw = 0x00002083U,
        .rd = 1U,
        .rs1 = 0,
        .rs2 = 0,
        .immediate = 0,
    };

    const auto pending =
        rv32::execute_decoded(decoded, 0x80000000U, 0, 0);

    CHECK(
        pending.status ==
        rv32::ExecuteStatus::UnsupportedInstruction);
    CHECK(!pending.ready());
    CHECK(pending.pc == 0x80000000U);
    CHECK(pending.instruction == 0x00002083U);
    CHECK(pending.next_pc == 0x80000000U);
    CHECK(!pending.register_write.enabled);
}

void test_fence_retires_without_a_register_write()
{
    const rv32::DecodedInstruction decoded{
        .kind = rv32::InstructionKind::Fence,
        .raw = 0x0000000FU,
        .rd = 0,
        .rs1 = 0,
        .rs2 = 0,
        .immediate = 0,
    };

    const auto pending =
        rv32::execute_decoded(decoded, 0x80000000U, 0, 0);

    CHECK(pending.ready());
    CHECK(pending.status == rv32::ExecuteStatus::Ready);
    CHECK(pending.pc == 0x80000000U);
    CHECK(pending.instruction == 0x0000000FU);
    CHECK(pending.next_pc == 0x80000004U);
    CHECK(!pending.register_write.enabled);
}

void test_environment_call_and_breakpoint_are_not_committable()
{
    const rv32::DecodedInstruction ecall{
        .kind = rv32::InstructionKind::Ecall,
        .raw = 0x00000073U,
    };
    const auto ecall_result =
        rv32::execute_decoded(ecall, 0x80000000U, 0, 0);
    CHECK(
        ecall_result.status ==
        rv32::ExecuteStatus::EnvironmentCall);
    CHECK(!ecall_result.ready());
    CHECK(ecall_result.next_pc == 0x80000000U);
    CHECK(!ecall_result.register_write.enabled);

    const rv32::DecodedInstruction ebreak{
        .kind = rv32::InstructionKind::Ebreak,
        .raw = 0x00100073U,
    };
    const auto ebreak_result =
        rv32::execute_decoded(ebreak, 0x80000000U, 0, 0);
    CHECK(
        ebreak_result.status ==
        rv32::ExecuteStatus::Breakpoint);
    CHECK(!ebreak_result.ready());
    CHECK(ebreak_result.next_pc == 0x80000000U);
    CHECK(!ebreak_result.register_write.enabled);
}

void test_ready_commit_updates_state_once()
{
    rv32::CpuSnapshot state{};
    state.pc = 0x80000000U;
    state.registers[5] = 0x11111111U;
    state.cycle = 27U;
    state.instructions_retired = 9U;

    const rv32::PendingCommit pending{
        .status = rv32::ExecuteStatus::Ready,
        .pc = 0x80000000U,
        .instruction = 0x02A00293U,
        .next_pc = 0x80000004U,
        .register_write = {
            .enabled = true,
            .index = 5U,
            .value = 42U,
        },
    };

    CHECK(rv32::commit_pending(state, pending));
    CHECK(state.pc == 0x80000004U);
    CHECK(state.registers[5] == 42U);
    CHECK(state.registers[0] == 0U);
    CHECK(state.instructions_retired == 10U);
    CHECK(state.cycle == 27U);
}

void test_x0_write_is_discarded()
{
    rv32::CpuSnapshot state{};
    state.pc = 0x80000000U;
    state.registers[0] = 0xFFFFFFFFU;

    const rv32::PendingCommit pending{
        .status = rv32::ExecuteStatus::Ready,
        .pc = 0x80000000U,
        .instruction = 0x00100013U,
        .next_pc = 0x80000004U,
        .register_write = {
            .enabled = true,
            .index = 0,
            .value = 1U,
        },
    };

    CHECK(rv32::commit_pending(state, pending));
    CHECK(state.pc == 0x80000004U);
    CHECK(state.registers[0] == 0U);
    CHECK(state.instructions_retired == 1U);
}

void test_invalid_commits_are_atomic()
{
    rv32::CpuSnapshot state{};
    state.pc = 0x80000000U;
    state.registers[3] = 0x12345678U;
    state.cycle = 11U;
    state.instructions_retired = 4U;

    const auto original = state;

    const rv32::PendingCommit not_ready{
        .status = rv32::ExecuteStatus::UnsupportedInstruction,
        .pc = state.pc,
        .instruction = 0,
        .next_pc = state.pc + 4U,
        .register_write = {
            .enabled = true,
            .index = 3U,
            .value = 0,
        },
    };
    CHECK(!rv32::commit_pending(state, not_ready));
    CHECK(same_snapshot(state, original));

    const rv32::PendingCommit stale{
        .status = rv32::ExecuteStatus::Ready,
        .pc = state.pc + 4U,
        .instruction = 0,
        .next_pc = state.pc + 8U,
        .register_write = {},
    };
    CHECK(!rv32::commit_pending(state, stale));
    CHECK(same_snapshot(state, original));

    const rv32::PendingCommit invalid_rd{
        .status = rv32::ExecuteStatus::Ready,
        .pc = state.pc,
        .instruction = 0,
        .next_pc = state.pc + 4U,
        .register_write = {
            .enabled = true,
            .index = 32U,
            .value = 0,
        },
    };
    CHECK(!rv32::commit_pending(state, invalid_rd));
    CHECK(same_snapshot(state, original));

    const rv32::PendingCommit invalid_privilege{
        .status = rv32::ExecuteStatus::Ready,
        .pc = state.pc,
        .instruction = 0x30200073U,
        .next_pc = state.pc + 4U,
        .register_write = {
            .enabled = true,
            .index = 3U,
            .value = 0U,
        },
        .csr_write = {},
        .privilege_write = {
            .enabled = true,
            .value = static_cast<rv32::PrivilegeMode>(2U),
        },
    };
    CHECK(!rv32::commit_pending(state, invalid_privilege));
    CHECK(same_snapshot(state, original));
}

} // namespace

int main()
{
    test_integer_execution();
    test_next_pc_wraps();
    test_unsupported_instruction_is_not_committable();
    test_fence_retires_without_a_register_write();
    test_environment_call_and_breakpoint_are_not_committable();
    test_ready_commit_updates_state_once();
    test_x0_write_is_discarded();
    test_invalid_commits_are_atomic();

    if (failures == 0) {
        std::cout << "All RV32 execute/commit tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
