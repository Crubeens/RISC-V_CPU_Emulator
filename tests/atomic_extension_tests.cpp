#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "rv32/core/core.hpp"
#include "rv32/core/csr.hpp"
#include "rv32/core/execute.hpp"
#include "rv32/core/trap.hpp"
#include "rv32/devices/ram.hpp"
#include "rv32/platform/system_bus.hpp"

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

constexpr rv32::PhysAddr ram_base = 0x80000000ULL;
constexpr std::uint32_t data_address = 0x80000100U;
constexpr std::uint32_t pc = 0x80000000U;

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

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t immediate,
    std::uint32_t rd) noexcept
{
    return (immediate & 0xFFFFF000U) |
           ((rd & 0x1FU) << 7U) |
           0x37U;
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

void check_atomic_fault_trap(
    const rv32::CpuSnapshot& after,
    const rv32::CpuSnapshot& before,
    rv32::ExceptionCause cause)
{
    CHECK(after.registers == before.registers);
    CHECK(after.pc == 0U);
    CHECK(after.privilege == rv32::PrivilegeMode::Machine);
    CHECK(after.machine_csrs.mepc == before.pc);
    CHECK(
        after.machine_csrs.mcause ==
        static_cast<std::uint32_t>(cause));
    CHECK(
        after.machine_csrs.mstatus ==
        (static_cast<std::uint32_t>(
             rv32::PrivilegeMode::Machine)
         << rv32::mstatus_bits::mpp_shift));
    CHECK(
        after.instructions_retired ==
        before.instructions_retired);
}

[[nodiscard]] std::uint32_t read_word(
    rv32::platform::SystemBus& bus,
    rv32::PhysAddr address)
{
    const auto result = bus.read(
        address,
        rv32::AccessWidth::Word,
        rv32::AccessKind::Load);
    CHECK(result.ok());
    return static_cast<std::uint32_t>(result.value);
}

struct AmoCase {
    std::string_view name;
    std::uint32_t funct5{};
    rv32::InstructionKind kind{rv32::InstructionKind::Illegal};
    std::uint32_t initial{};
    std::uint32_t operand{};
    std::uint32_t replacement{};
};

constexpr std::array amo_cases{
    AmoCase{
        "AMOSWAP.W normal",
        0x01U,
        rv32::InstructionKind::AmoSwapW,
        0x12345678U,
        0x87654321U,
        0x87654321U,
    },
    AmoCase{
        "AMOSWAP.W zero boundary",
        0x01U,
        rv32::InstructionKind::AmoSwapW,
        0xFFFFFFFFU,
        0U,
        0U,
    },
    AmoCase{
        "AMOADD.W normal",
        0x00U,
        rv32::InstructionKind::AmoAddW,
        10U,
        7U,
        17U,
    },
    AmoCase{
        "AMOADD.W wraps",
        0x00U,
        rv32::InstructionKind::AmoAddW,
        0xFFFFFFFFU,
        2U,
        1U,
    },
    AmoCase{
        "AMOXOR.W normal",
        0x04U,
        rv32::InstructionKind::AmoXorW,
        0xAAAA5555U,
        0x0F0FF0F0U,
        0xA5A5A5A5U,
    },
    AmoCase{
        "AMOXOR.W equal operands",
        0x04U,
        rv32::InstructionKind::AmoXorW,
        0xA5A5A5A5U,
        0xA5A5A5A5U,
        0U,
    },
    AmoCase{
        "AMOAND.W normal",
        0x0CU,
        rv32::InstructionKind::AmoAndW,
        0xAFF05FF0U,
        0x0FF00FF0U,
        0x0FF00FF0U,
    },
    AmoCase{
        "AMOAND.W zero operand",
        0x0CU,
        rv32::InstructionKind::AmoAndW,
        0xFFFFFFFFU,
        0U,
        0U,
    },
    AmoCase{
        "AMOOR.W normal",
        0x08U,
        rv32::InstructionKind::AmoOrW,
        0xA0005000U,
        0x0A00500AU,
        0xAA00500AU,
    },
    AmoCase{
        "AMOOR.W all-ones operand",
        0x08U,
        rv32::InstructionKind::AmoOrW,
        0U,
        0xFFFFFFFFU,
        0xFFFFFFFFU,
    },
    AmoCase{
        "AMOMIN.W normal signed comparison",
        0x10U,
        rv32::InstructionKind::AmoMinW,
        3U,
        0xFFFFFFFEU,
        0xFFFFFFFEU,
    },
    AmoCase{
        "AMOMIN.W signed boundary",
        0x10U,
        rv32::InstructionKind::AmoMinW,
        0x80000000U,
        0x7FFFFFFFU,
        0x80000000U,
    },
    AmoCase{
        "AMOMAX.W normal signed comparison",
        0x14U,
        rv32::InstructionKind::AmoMaxW,
        0xFFFFFFFDU,
        2U,
        2U,
    },
    AmoCase{
        "AMOMAX.W signed boundary",
        0x14U,
        rv32::InstructionKind::AmoMaxW,
        0x80000000U,
        0x7FFFFFFFU,
        0x7FFFFFFFU,
    },
    AmoCase{
        "AMOMINU.W normal",
        0x18U,
        rv32::InstructionKind::AmoMinuW,
        3U,
        2U,
        2U,
    },
    AmoCase{
        "AMOMINU.W unsigned boundary",
        0x18U,
        rv32::InstructionKind::AmoMinuW,
        0xFFFFFFFFU,
        0U,
        0U,
    },
    AmoCase{
        "AMOMAXU.W normal",
        0x1CU,
        rv32::InstructionKind::AmoMaxuW,
        3U,
        2U,
        3U,
    },
    AmoCase{
        "AMOMAXU.W unsigned boundary",
        0x1CU,
        rv32::InstructionKind::AmoMaxuW,
        0x80000000U,
        0x7FFFFFFFU,
        0x80000000U,
    },
};

void test_all_word_amo_operations()
{
    constexpr std::uint32_t rd = 7U;

    for (const auto& test : amo_cases) {
        rv32::platform::SystemBus bus;
        static_cast<void>(
            bus.emplace_device<rv32::devices::Ram>(
                ram_base,
                0x1000U));
        CHECK(
            bus.write(
                data_address,
                rv32::AccessWidth::Word,
                test.initial,
                rv32::AccessKind::Store) ==
            rv32::BusFault::None);

        const std::uint32_t raw = encode_a(
            test.funct5,
            false,
            false,
            2U,
            1U,
            rd);
        const auto decoded = rv32::decode_instruction(raw);
        const auto result = rv32::execute_atomic(
            bus,
            decoded,
            pc,
            4U,
            data_address,
            test.operand);

        rv32::CpuSnapshot state{};
        state.pc = pc;
        const bool committed =
            rv32::commit_pending(state, result.pending);
        const std::uint32_t memory =
            read_word(bus, data_address);

        const bool passed =
            decoded.kind == test.kind &&
            result.ready() &&
            result.status == rv32::AtomicStatus::Ready &&
            result.bus_fault == rv32::BusFault::None &&
            result.trap_value == 0U &&
            result.pending.register_write.enabled &&
            result.pending.register_write.index == rd &&
            result.pending.register_write.value == test.initial &&
            committed &&
            state.registers[rd] == test.initial &&
            state.pc == pc + 4U &&
            state.instructions_retired == 1U &&
            memory == test.replacement;

        if (!passed) {
            std::cerr
                << "FAIL AMO case \"" << test.name
                << "\": expected memory=0x" << std::hex
                << std::setw(8) << std::setfill('0')
                << test.replacement
                << ", actual memory=0x" << std::setw(8)
                << memory
                << ", expected rd=0x" << std::setw(8)
                << test.initial
                << ", actual rd=0x" << std::setw(8)
                << result.pending.register_write.value
                << std::setfill(' ') << std::dec
                << ", decoded="
                << static_cast<unsigned int>(decoded.kind)
                << ", ready=" << result.ready()
                << ", committed=" << committed
                << '\n';
            ++failures;
        }
    }
}

void test_lr_sc_success_failure_and_invalidation()
{
    rv32::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv32::devices::Ram>(
            ram_base,
            0x1000U));
    CHECK(
        bus.write(
            data_address,
            rv32::AccessWidth::Word,
            0x11223344U,
            rv32::AccessKind::Store) ==
        rv32::BusFault::None);

    const auto lr = rv32::decode_instruction(
        encode_a(0x02U, true, false, 0U, 1U, 5U));
    const auto sc = rv32::decode_instruction(
        encode_a(0x03U, false, true, 2U, 1U, 6U));

    const auto loaded = rv32::execute_atomic(
        bus,
        lr,
        pc,
        3U,
        data_address,
        0U);
    CHECK(loaded.ready());
    CHECK(loaded.pending.register_write.value == 0x11223344U);
    CHECK(read_word(bus, data_address) == 0x11223344U);

    const auto successful = rv32::execute_atomic(
        bus,
        sc,
        pc + 4U,
        3U,
        data_address,
        0x55667788U);
    CHECK(successful.ready());
    CHECK(successful.pending.register_write.value == 0U);
    CHECK(read_word(bus, data_address) == 0x55667788U);

    const auto failed_without_lr = rv32::execute_atomic(
        bus,
        sc,
        pc + 8U,
        3U,
        data_address,
        0xDEADBEEFU);
    CHECK(failed_without_lr.ready());
    CHECK(failed_without_lr.pending.register_write.value == 1U);
    CHECK(read_word(bus, data_address) == 0x55667788U);

    CHECK(
        rv32::execute_atomic(
            bus,
            lr,
            pc,
            3U,
            data_address,
            0U)
            .ready());
    const auto wrong_address = rv32::execute_atomic(
        bus,
        sc,
        pc + 4U,
        3U,
        data_address + 4U,
        1U);
    CHECK(wrong_address.ready());
    CHECK(wrong_address.pending.register_write.value == 1U);

    const auto reservation_was_consumed = rv32::execute_atomic(
        bus,
        sc,
        pc + 8U,
        3U,
        data_address,
        2U);
    CHECK(reservation_was_consumed.ready());
    CHECK(
        reservation_was_consumed.pending.register_write.value ==
        1U);

    CHECK(
        rv32::execute_atomic(
            bus,
            lr,
            pc,
            3U,
            data_address,
            0U)
            .ready());
    CHECK(
        bus.write(
            data_address + 12U,
            rv32::AccessWidth::Word,
            0x5A5A5A5AU,
            rv32::AccessKind::Store) ==
        rv32::BusFault::None);
    const auto invalidated_by_store = rv32::execute_atomic(
        bus,
        sc,
        pc + 4U,
        3U,
        data_address,
        3U);
    CHECK(invalidated_by_store.ready());
    CHECK(invalidated_by_store.pending.register_write.value == 1U);

    CHECK(
        rv32::execute_atomic(
            bus,
            lr,
            pc,
            3U,
            data_address,
            0U)
            .ready());
    CHECK(
        bus.dma_write(
            data_address + 8U,
            rv32::AccessWidth::Word,
            0xA5A5A5A5U) ==
        rv32::BusFault::None);
    const auto invalidated_by_dma = rv32::execute_atomic(
        bus,
        sc,
        pc + 4U,
        3U,
        data_address,
        3U);
    CHECK(invalidated_by_dma.ready());
    CHECK(invalidated_by_dma.pending.register_write.value == 1U);
}

void test_atomic_fault_mapping()
{
    rv32::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv32::devices::Ram>(
            ram_base,
            0x1000U));

    const auto lr = rv32::decode_instruction(
        encode_a(0x02U, false, false, 0U, 1U, 5U));
    const auto sc = rv32::decode_instruction(
        encode_a(0x03U, false, false, 2U, 1U, 5U));
    const auto amo = rv32::decode_instruction(
        encode_a(0x00U, false, false, 2U, 1U, 5U));

    const auto misaligned_lr = rv32::execute_atomic(
        bus,
        lr,
        pc,
        0U,
        data_address + 2U,
        0U);
    CHECK(
        misaligned_lr.status ==
        rv32::AtomicStatus::LoadAddressMisaligned);
    CHECK(misaligned_lr.bus_fault == rv32::BusFault::Misaligned);
    CHECK(misaligned_lr.trap_value == data_address + 2U);
    CHECK(!misaligned_lr.pending.ready());

    const auto misaligned_sc = rv32::execute_atomic(
        bus,
        sc,
        pc,
        0U,
        data_address + 2U,
        1U);
    CHECK(
        misaligned_sc.status ==
        rv32::AtomicStatus::StoreAddressMisaligned);
    CHECK(misaligned_sc.bus_fault == rv32::BusFault::Misaligned);
    CHECK(misaligned_sc.trap_value == data_address + 2U);
    CHECK(!misaligned_sc.pending.ready());

    const auto misaligned_amo = rv32::execute_atomic(
        bus,
        amo,
        pc,
        0U,
        data_address + 2U,
        1U);
    CHECK(
        misaligned_amo.status ==
        rv32::AtomicStatus::StoreAddressMisaligned);
    CHECK(misaligned_amo.bus_fault == rv32::BusFault::Misaligned);
    CHECK(!misaligned_amo.pending.ready());

    constexpr std::uint32_t unmapped = 0x90000000U;
    const auto unmapped_lr = rv32::execute_atomic(
        bus,
        lr,
        pc,
        0U,
        unmapped,
        0U);
    CHECK(
        unmapped_lr.status ==
        rv32::AtomicStatus::LoadAccessFault);
    CHECK(unmapped_lr.bus_fault == rv32::BusFault::Unmapped);
    CHECK(unmapped_lr.trap_value == unmapped);
    CHECK(!unmapped_lr.pending.ready());

    const auto unmapped_sc = rv32::execute_atomic(
        bus,
        sc,
        pc,
        0U,
        unmapped,
        1U);
    CHECK(
        unmapped_sc.status ==
        rv32::AtomicStatus::StoreAccessFault);
    CHECK(unmapped_sc.bus_fault == rv32::BusFault::Unmapped);
    CHECK(unmapped_sc.trap_value == unmapped);
    CHECK(!unmapped_sc.pending.ready());

    const auto unmapped_amo = rv32::execute_atomic(
        bus,
        amo,
        pc,
        0U,
        unmapped,
        1U);
    CHECK(
        unmapped_amo.status ==
        rv32::AtomicStatus::StoreAccessFault);
    CHECK(unmapped_amo.bus_fault == rv32::BusFault::Unmapped);
    CHECK(unmapped_amo.trap_value == unmapped);
    CHECK(!unmapped_amo.pending.ready());

    const rv32::DecodedInstruction addi{
        .kind = rv32::InstructionKind::Addi,
        .raw = 0x00000013U,
    };
    const auto not_atomic = rv32::execute_atomic(
        bus,
        addi,
        pc,
        0U,
        data_address,
        0U);
    CHECK(
        not_atomic.status ==
        rv32::AtomicStatus::NotAtomicInstruction);
    CHECK(!not_atomic.pending.ready());
    CHECK(not_atomic.bus_fault == rv32::BusFault::None);
    CHECK(not_atomic.trap_value == 0U);
}

void test_atomic_rd_x0_keeps_memory_side_effect()
{
    rv32::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv32::devices::Ram>(
            ram_base,
            0x1000U));
    CHECK(
        bus.write(
            data_address,
            rv32::AccessWidth::Word,
            0x11111111U,
            rv32::AccessKind::Store) ==
        rv32::BusFault::None);

    const auto swap_to_x0 = rv32::decode_instruction(
        encode_a(0x01U, false, false, 2U, 1U, 0U));
    const auto result = rv32::execute_atomic(
        bus,
        swap_to_x0,
        pc,
        0U,
        data_address,
        0x22222222U);

    rv32::CpuSnapshot state{};
    state.pc = pc;
    state.registers[0] = 0xFFFFFFFFU;
    CHECK(rv32::commit_pending(state, result.pending));
    CHECK(state.registers[0] == 0U);
    CHECK(read_word(bus, data_address) == 0x22222222U);
}

void test_atomic_instructions_flow_through_core_step()
{
    rv32::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv32::devices::Ram>(
            ram_base,
            0x2000U));

    constexpr std::array program{
        encode_u(0x80000000U, 1U),
        encode_i(0x100U, 1U, 0U, 1U, 0x13U),
        encode_i(5U, 0U, 0U, 2U, 0x13U),
        encode_a(0x02U, true, false, 0U, 1U, 3U),
        encode_a(0x03U, false, true, 2U, 1U, 4U),
        encode_a(0x00U, true, true, 2U, 1U, 5U),
    };

    for (std::size_t index = 0; index < program.size(); ++index) {
        CHECK(
            bus.write(
                ram_base + index * 4U,
                rv32::AccessWidth::Word,
                program[index],
                rv32::AccessKind::Store) ==
            rv32::BusFault::None);
    }
    CHECK(
        bus.write(
            data_address,
            rv32::AccessWidth::Word,
            10U,
            rv32::AccessKind::Store) ==
        rv32::BusFault::None);

    rv32::Core core(bus);
    for (std::size_t index = 0; index < program.size(); ++index) {
        const auto result = core.step({});
        CHECK(result.status == rv32::StepStatus::Retired);
        CHECK(
            result.pc ==
            pc + static_cast<std::uint32_t>(index * 4U));
        CHECK(result.instruction == program[index]);
        if (result.status != rv32::StepStatus::Retired) {
            return;
        }
    }

    const auto state = core.snapshot();
    CHECK(state.registers[1] == data_address);
    CHECK(state.registers[2] == 5U);
    CHECK(state.registers[3] == 10U);
    CHECK(state.registers[4] == 0U);
    CHECK(state.registers[5] == 5U);
    CHECK(read_word(bus, data_address) == 10U);
    CHECK(state.pc == pc + program.size() * 4U);
    CHECK(state.instructions_retired == program.size());
}

void test_core_step_atomic_faults_are_precise()
{
    struct FaultCase {
        std::uint32_t instruction{};
        rv32::ExceptionCause cause{
            rv32::ExceptionCause::IllegalInstruction};
    };
    constexpr std::array cases{
        FaultCase{
            encode_a(0x02U, false, false, 0U, 1U, 2U),
            rv32::ExceptionCause::LoadAccessFault,
        },
        FaultCase{
            encode_a(0x03U, false, false, 0U, 1U, 2U),
            rv32::ExceptionCause::StoreAccessFault,
        },
        FaultCase{
            encode_a(0x00U, false, false, 0U, 1U, 2U),
            rv32::ExceptionCause::StoreAccessFault,
        },
    };

    for (const auto& test : cases) {
        rv32::platform::SystemBus bus;
        static_cast<void>(
            bus.emplace_device<rv32::devices::Ram>(
                ram_base,
                0x1000U));
        CHECK(
            bus.write(
                ram_base,
                rv32::AccessWidth::Word,
                encode_u(0x90000000U, 1U),
                rv32::AccessKind::Store) ==
            rv32::BusFault::None);
        CHECK(
            bus.write(
                ram_base + 4U,
                rv32::AccessWidth::Word,
                test.instruction,
                rv32::AccessKind::Store) ==
            rv32::BusFault::None);

        rv32::Core core(bus);
        CHECK(core.step({}).status == rv32::StepStatus::Retired);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.pc == pc + 4U);
        CHECK(result.instruction == test.instruction);
        CHECK(result.trap_value == 0x90000000U);
        CHECK(result.bus_fault == rv32::BusFault::Unmapped);
        CHECK(core.snapshot().machine_csrs.mtval == 0x90000000U);
        check_atomic_fault_trap(
            core.snapshot(),
            before,
            test.cause);
    }
}

} // namespace

int main()
{
    test_all_word_amo_operations();
    test_lr_sc_success_failure_and_invalidation();
    test_atomic_fault_mapping();
    test_atomic_rd_x0_keeps_memory_side_effect();
    test_atomic_instructions_flow_through_core_step();
    test_core_step_atomic_faults_are_precise();

    if (failures == 0) {
        std::cout << "All RV32A atomic extension tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
