#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "rv32/core/core.hpp"
#include "rv32/core/csr.hpp"
#include "rv32/core/trap.hpp"

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

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t immediate,
    std::uint32_t rd,
    std::uint32_t opcode) noexcept
{
    return (immediate & 0xFFFFF000U) |
           ((rd & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_b(
    std::uint32_t immediate,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3) noexcept
{
    return (((immediate >> 12U) & 0x1U) << 31U) |
           (((immediate >> 5U) & 0x3FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           (((immediate >> 1U) & 0xFU) << 8U) |
           (((immediate >> 11U) & 0x1U) << 7U) |
           0x63U;
}

[[nodiscard]] constexpr std::uint32_t encode_j(
    std::uint32_t immediate,
    std::uint32_t rd) noexcept
{
    return (((immediate >> 20U) & 0x1U) << 31U) |
           (((immediate >> 1U) & 0x3FFU) << 21U) |
           (((immediate >> 11U) & 0x1U) << 20U) |
           (((immediate >> 12U) & 0xFFU) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x6FU;
}

class ProgramBus final : public rv32::CpuBus {
  public:
    static constexpr std::uint32_t base = 0x80000000U;

    std::array<std::uint32_t, 8> words{};
    rv32::BusFault forced_fault{rv32::BusFault::None};
    rv32::ReadResult next_load{};
    rv32::BusFault next_store{rv32::BusFault::None};
    std::uint32_t read_count{};
    std::uint32_t write_count{};
    std::uint32_t instruction_fetch_count{};
    std::uint32_t load_count{};
    std::uint32_t store_count{};
    rv32::PhysAddr last_data_address{};
    rv32::AccessWidth last_data_width{rv32::AccessWidth::Byte};
    std::uint64_t last_store_value{};

    rv32::ReadResult read(
        rv32::PhysAddr address,
        rv32::AccessWidth width,
        rv32::AccessKind kind) override
    {
        ++read_count;

        if (kind == rv32::AccessKind::Load) {
            ++load_count;
            last_data_address = address;
            last_data_width = width;
            return next_load;
        }
        if (kind != rv32::AccessKind::InstructionFetch ||
            width != rv32::AccessWidth::Word) {
            return {.fault = rv32::BusFault::Unsupported};
        }

        ++instruction_fetch_count;
        if (forced_fault != rv32::BusFault::None) {
            return {.fault = forced_fault};
        }
        if (address < base) {
            return {.fault = rv32::BusFault::Unmapped};
        }

        const rv32::PhysAddr offset = address - base;
        if ((offset & 0x3U) != 0U) {
            return {.fault = rv32::BusFault::Misaligned};
        }

        const rv32::PhysAddr index = offset / 4U;
        if (index >= words.size()) {
            return {.fault = rv32::BusFault::Unmapped};
        }
        return {
            .fault = rv32::BusFault::None,
            .value = words[static_cast<std::size_t>(index)],
        };
    }

    rv32::BusFault write(
        rv32::PhysAddr address,
        rv32::AccessWidth width,
        std::uint64_t value,
        rv32::AccessKind kind) override
    {
        ++write_count;
        if (kind != rv32::AccessKind::Store) {
            return rv32::BusFault::Unsupported;
        }

        ++store_count;
        last_data_address = address;
        last_data_width = width;
        last_store_value = value;
        return next_store;
    }

    rv32::ReadResult load_reserved_word(
        std::uint32_t,
        rv32::PhysAddr) override
    {
        return {.fault = rv32::BusFault::DeviceError};
    }

    rv32::StoreConditionalResult store_conditional_word(
        std::uint32_t,
        rv32::PhysAddr,
        std::uint32_t) override
    {
        return {
            .fault = rv32::BusFault::DeviceError,
            .succeeded = false,
        };
    }

    rv32::AtomicResult atomic_word(
        std::uint32_t,
        rv32::PhysAddr,
        rv32::AmoOperation,
        std::uint32_t) override
    {
        return {.fault = rv32::BusFault::DeviceError};
    }

    std::uint64_t read_time() const noexcept override
    {
        return 0;
    }
};

void test_reset_initializes_boot_arguments()
{
    ProgramBus bus;
    rv32::Core core(bus);
    core.reset({
        .reset_pc = ProgramBus::base + 0x100U,
        .hart_id = 7U,
        .initial_privilege = rv32::PrivilegeMode::Machine,
        .boot_argument = 0x83FFF000U,
    });

    const auto state = core.snapshot();
    CHECK(state.pc == ProgramBus::base + 0x100U);
    CHECK(state.hart_id == 7U);
    CHECK(state.registers[0] == 0U);
    CHECK(state.registers[10] == state.hart_id);
    CHECK(state.registers[11] == 0x83FFF000U);
    for (std::size_t index = 1; index < state.registers.size(); ++index) {
        if (index != 10U && index != 11U) {
            CHECK(state.registers[index] == 0U);
        }
    }
}

void check_precise_machine_trap(
    const rv32::CpuSnapshot& after,
    const rv32::CpuSnapshot& before,
    rv32::ExceptionCause cause,
    std::uint32_t trap_value)
{
    std::uint32_t expected_mstatus =
        rv32::sanitize_mstatus(before.machine_csrs.mstatus);
    const bool mie =
        (expected_mstatus & rv32::mstatus_bits::mie) != 0U;
    expected_mstatus &=
        ~(rv32::mstatus_bits::mie |
          rv32::mstatus_bits::mpie |
          rv32::mstatus_bits::mpp);
    if (mie) {
        expected_mstatus |= rv32::mstatus_bits::mpie;
    }
    expected_mstatus |=
        static_cast<std::uint32_t>(before.privilege)
        << rv32::mstatus_bits::mpp_shift;

    CHECK(after.registers == before.registers);
    CHECK(after.pc == (before.machine_csrs.mtvec & ~0x3U));
    CHECK(after.hart_id == before.hart_id);
    CHECK(after.privilege == rv32::PrivilegeMode::Machine);
    CHECK(after.machine_csrs.mstatus == expected_mstatus);
    CHECK(
        after.machine_csrs.medeleg ==
        before.machine_csrs.medeleg);
    CHECK(
        after.machine_csrs.mideleg ==
        before.machine_csrs.mideleg);
    CHECK(after.machine_csrs.mtvec == before.machine_csrs.mtvec);
    CHECK(after.machine_csrs.mscratch == before.machine_csrs.mscratch);
    CHECK(after.machine_csrs.mepc == (before.pc & ~0x3U));
    CHECK(
        after.machine_csrs.mcause ==
        static_cast<std::uint32_t>(cause));
    CHECK(after.machine_csrs.mtval == trap_value);
    CHECK(after.supervisor_csrs == before.supervisor_csrs);
    CHECK(after.cycle == before.cycle);
    CHECK(
        after.instructions_retired ==
        before.instructions_retired);
    CHECK(!after.waiting_for_interrupt);
}

void test_register_values_flow_through_complete_steps()
{
    ProgramBus bus;
    bus.words[0] = 0x00500093U; // addi x1, x0, 5
    bus.words[1] = 0xFFE08113U; // addi x2, x1, -2
    bus.words[2] = 0x002081B3U; // add  x3, x1, x2
    bus.words[3] = 0x00100013U; // addi x0, x0, 1

    rv32::Core core(bus);
    constexpr std::array expected_instructions{
        0x00500093U,
        0xFFE08113U,
        0x002081B3U,
        0x00100013U,
    };

    for (std::size_t index = 0;
         index < expected_instructions.size();
         ++index) {
        const auto result = core.step({});
        CHECK(result.status == rv32::StepStatus::Retired);
        if (result.status != rv32::StepStatus::Retired) {
            return;
        }
        CHECK(
            result.pc ==
            ProgramBus::base +
                static_cast<std::uint32_t>(index * 4U));
        CHECK(result.instruction == expected_instructions[index]);
        CHECK(result.trap_value == 0U);
        CHECK(result.bus_fault == rv32::BusFault::None);
    }

    const auto state = core.snapshot();
    CHECK(state.pc == ProgramBus::base + 16U);
    CHECK(state.registers[0] == 0U);
    CHECK(state.registers[1] == 5U);
    CHECK(state.registers[2] == 3U);
    CHECK(state.registers[3] == 8U);
    CHECK(state.instructions_retired == 4U);
    CHECK(bus.read_count == 4U);
    CHECK(bus.write_count == 0U);
}

void test_m_extension_flows_through_complete_steps()
{
    ProgramBus bus;
    bus.words[0] =
        encode_i(20U, 0U, 0U, 1U, 0x13U); // addi x1, x0, 20
    bus.words[1] =
        encode_i(3U, 0U, 0U, 2U, 0x13U); // addi x2, x0, 3
    bus.words[2] =
        encode_r(1U, 2U, 1U, 0U, 3U, 0x33U); // mul x3, x1, x2
    bus.words[3] =
        encode_r(1U, 2U, 1U, 4U, 4U, 0x33U); // div x4, x1, x2

    rv32::Core core(bus);
    for (std::size_t index = 0; index < 4U; ++index) {
        const auto result = core.step({});
        CHECK(result.status == rv32::StepStatus::Retired);
        CHECK(
            result.pc ==
            ProgramBus::base +
                static_cast<std::uint32_t>(index * 4U));
        if (result.status != rv32::StepStatus::Retired) {
            return;
        }
    }

    const auto state = core.snapshot();
    CHECK(state.registers[1] == 20U);
    CHECK(state.registers[2] == 3U);
    CHECK(state.registers[3] == 60U);
    CHECK(state.registers[4] == 6U);
    CHECK(state.pc == ProgramBus::base + 16U);
    CHECK(state.instructions_retired == 4U);
    CHECK(bus.instruction_fetch_count == 4U);
    CHECK(bus.load_count == 0U);
    CHECK(bus.store_count == 0U);
}

void test_misaligned_pc_does_not_change_state()
{
    ProgramBus bus;
    rv32::Core core(bus);
    core.reset({.reset_pc = ProgramBus::base + 2U});
    const auto before = core.snapshot();

    const auto result = core.step({});

    CHECK(result.status == rv32::StepStatus::TrapTaken);
    CHECK(result.pc == ProgramBus::base + 2U);
    CHECK(result.instruction == 0U);
    CHECK(result.trap_value == ProgramBus::base + 2U);
    CHECK(result.bus_fault == rv32::BusFault::Misaligned);
    check_precise_machine_trap(
        core.snapshot(),
        before,
        rv32::ExceptionCause::InstructionAddressMisaligned,
        ProgramBus::base + 2U);
    CHECK(bus.read_count == 0U);
}

void test_fetch_fault_does_not_change_state()
{
    constexpr std::array faults{
        rv32::BusFault::Unmapped,
        rv32::BusFault::OutOfRange,
        rv32::BusFault::ReadOnly,
        rv32::BusFault::Unsupported,
        rv32::BusFault::DeviceError,
    };

    for (const auto fault : faults) {
        ProgramBus bus;
        bus.forced_fault = fault;
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.pc == ProgramBus::base);
        CHECK(result.instruction == 0U);
        CHECK(result.trap_value == ProgramBus::base);
        CHECK(result.bus_fault == fault);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            rv32::ExceptionCause::InstructionAccessFault,
            ProgramBus::base);
        CHECK(bus.read_count == 1U);
    }
}

void test_illegal_instruction_does_not_change_state()
{
    ProgramBus bus;
    bus.words[0] = 0xFFFFFFFFU;
    rv32::Core core(bus);
    const auto before = core.snapshot();

    const auto result = core.step({});

    CHECK(result.status == rv32::StepStatus::TrapTaken);
    CHECK(result.pc == ProgramBus::base);
    CHECK(result.instruction == 0xFFFFFFFFU);
    CHECK(result.trap_value == 0xFFFFFFFFU);
    CHECK(result.bus_fault == rv32::BusFault::None);
    check_precise_machine_trap(
        core.snapshot(),
        before,
        rv32::ExceptionCause::IllegalInstruction,
        0xFFFFFFFFU);
    CHECK(bus.read_count == 1U);
}

void test_load_result_flows_into_store()
{
    ProgramBus bus;
    bus.words[0] = 0x00402083U; // lw x1, 4(x0)
    bus.words[1] = 0x00102423U; // sw x1, 8(x0)
    bus.next_load = {
        .fault = rv32::BusFault::None,
        .value = 0x89ABCDEFU,
    };
    rv32::Core core(bus);

    const auto load_result = core.step({});
    CHECK(load_result.status == rv32::StepStatus::Retired);
    CHECK(load_result.instruction == 0x00402083U);
    CHECK(core.snapshot().registers[1] == 0x89ABCDEFU);
    CHECK(core.snapshot().pc == ProgramBus::base + 4U);
    CHECK(core.snapshot().instructions_retired == 1U);
    CHECK(bus.instruction_fetch_count == 1U);
    CHECK(bus.load_count == 1U);
    CHECK(bus.store_count == 0U);
    CHECK(bus.last_data_address == 4U);
    CHECK(bus.last_data_width == rv32::AccessWidth::Word);

    const auto store_result = core.step({});
    CHECK(store_result.status == rv32::StepStatus::Retired);
    CHECK(store_result.instruction == 0x00102423U);
    CHECK(core.snapshot().registers[1] == 0x89ABCDEFU);
    CHECK(core.snapshot().pc == ProgramBus::base + 8U);
    CHECK(core.snapshot().instructions_retired == 2U);
    CHECK(bus.instruction_fetch_count == 2U);
    CHECK(bus.load_count == 1U);
    CHECK(bus.store_count == 1U);
    CHECK(bus.last_data_address == 8U);
    CHECK(bus.last_data_width == rv32::AccessWidth::Word);
    CHECK(bus.last_store_value == 0x89ABCDEFU);
}

void test_data_misalignment_does_not_commit()
{
    {
        ProgramBus bus;
        bus.words[0] = 0x00202083U; // lw x1, 2(x0)
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.trap_value == 2U);
        CHECK(result.bus_fault == rv32::BusFault::Misaligned);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            rv32::ExceptionCause::LoadAddressMisaligned,
            2U);
        CHECK(bus.instruction_fetch_count == 1U);
        CHECK(bus.load_count == 0U);
        CHECK(bus.store_count == 0U);
    }

    {
        ProgramBus bus;
        bus.words[0] = 0x00102123U; // sw x1, 2(x0)
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.trap_value == 2U);
        CHECK(result.bus_fault == rv32::BusFault::Misaligned);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            rv32::ExceptionCause::StoreAddressMisaligned,
            2U);
        CHECK(bus.instruction_fetch_count == 1U);
        CHECK(bus.load_count == 0U);
        CHECK(bus.store_count == 0U);
    }
}

void test_data_bus_faults_do_not_commit()
{
    constexpr std::array faults{
        rv32::BusFault::Unmapped,
        rv32::BusFault::OutOfRange,
        rv32::BusFault::ReadOnly,
        rv32::BusFault::Unsupported,
        rv32::BusFault::DeviceError,
    };

    for (const auto fault : faults) {
        ProgramBus bus;
        bus.words[0] = 0x00402083U; // lw x1, 4(x0)
        bus.next_load = {.fault = fault};
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.trap_value == 4U);
        CHECK(result.bus_fault == fault);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            rv32::ExceptionCause::LoadAccessFault,
            4U);
        CHECK(bus.load_count == 1U);
        CHECK(bus.store_count == 0U);
    }

    for (const auto fault : faults) {
        ProgramBus bus;
        bus.words[0] = 0x00102423U; // sw x1, 8(x0)
        bus.next_store = fault;
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.trap_value == 8U);
        CHECK(result.bus_fault == fault);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            rv32::ExceptionCause::StoreAccessFault,
            8U);
        CHECK(bus.load_count == 0U);
        CHECK(bus.store_count == 1U);
        CHECK(bus.write_count == 1U);
    }
}

void test_control_flow_runs_through_complete_steps()
{
    ProgramBus bus;
    bus.words[0] = encode_u(0U, 1U, 0x17U); // auipc x1, 0
    bus.words[1] =
        encode_i(1U, 0U, 0U, 2U, 0x13U); // addi x2, x0, 1
    bus.words[2] = encode_b(8U, 0U, 2U, 0U); // beq x2, x0, +8
    bus.words[3] = encode_b(8U, 0U, 2U, 1U); // bne x2, x0, +8
    bus.words[4] = 0xFFFFFFFFU; // skipped by BNE
    bus.words[5] = encode_j(8U, 5U); // jal x5, +8
    bus.words[6] = 0xFFFFFFFFU; // skipped by JAL
    bus.words[7] =
        encode_i(0U, 1U, 0U, 6U, 0x67U); // jalr x6, 0(x1)

    rv32::Core core(bus);
    constexpr std::array expected_pcs{
        ProgramBus::base,
        ProgramBus::base + 4U,
        ProgramBus::base + 8U,
        ProgramBus::base + 12U,
        ProgramBus::base + 20U,
        ProgramBus::base + 28U,
    };

    for (const std::uint32_t expected_pc : expected_pcs) {
        const auto result = core.step({});
        CHECK(result.status == rv32::StepStatus::Retired);
        CHECK(result.pc == expected_pc);
        CHECK(result.trap_value == 0U);
        CHECK(result.bus_fault == rv32::BusFault::None);
        if (result.status != rv32::StepStatus::Retired) {
            return;
        }
    }

    const auto state = core.snapshot();
    CHECK(state.pc == ProgramBus::base);
    CHECK(state.registers[1] == ProgramBus::base);
    CHECK(state.registers[2] == 1U);
    CHECK(state.registers[5] == ProgramBus::base + 24U);
    CHECK(state.registers[6] == ProgramBus::base + 32U);
    CHECK(state.instructions_retired == expected_pcs.size());
    CHECK(bus.instruction_fetch_count == expected_pcs.size());
    CHECK(bus.load_count == 0U);
    CHECK(bus.store_count == 0U);
}

void test_control_target_misalignment_is_atomic()
{
    {
        ProgramBus bus;
        bus.words[0] = encode_j(2U, 1U); // jal x1, +2
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.trap_value == ProgramBus::base + 2U);
        CHECK(result.bus_fault == rv32::BusFault::None);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            rv32::ExceptionCause::InstructionAddressMisaligned,
            ProgramBus::base + 2U);
        CHECK(bus.instruction_fetch_count == 1U);
    }

    {
        ProgramBus bus;
        bus.words[0] = encode_b(2U, 0U, 0U, 0U); // beq x0, x0, +2
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.trap_value == ProgramBus::base + 2U);
        CHECK(result.bus_fault == rv32::BusFault::None);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            rv32::ExceptionCause::InstructionAddressMisaligned,
            ProgramBus::base + 2U);
    }

    {
        ProgramBus bus;
        bus.words[0] = encode_b(2U, 0U, 0U, 1U); // bne x0, x0, +2
        rv32::Core core(bus);

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::Retired);
        CHECK(core.snapshot().pc == ProgramBus::base + 4U);
        CHECK(core.snapshot().instructions_retired == 1U);
    }

    {
        ProgramBus bus;
        bus.words[0] =
            encode_i(2U, 0U, 0U, 1U, 0x67U); // jalr x1, 2(x0)
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.trap_value == 2U);
        CHECK(result.bus_fault == rv32::BusFault::None);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            rv32::ExceptionCause::InstructionAddressMisaligned,
            2U);
    }
}

void test_fence_retires_through_complete_step()
{
    ProgramBus bus;
    bus.words[0] = 0x0000000FU; // fence
    rv32::Core core(bus);

    const auto result = core.step({});

    CHECK(result.status == rv32::StepStatus::Retired);
    CHECK(result.pc == ProgramBus::base);
    CHECK(result.instruction == 0x0000000FU);
    CHECK(result.trap_value == 0U);
    CHECK(result.bus_fault == rv32::BusFault::None);
    CHECK(core.snapshot().pc == ProgramBus::base + 4U);
    CHECK(core.snapshot().instructions_retired == 1U);
    CHECK(core.snapshot().registers[0] == 0U);
    CHECK(bus.read_count == 1U);
    CHECK(bus.write_count == 0U);
}

void test_ecall_and_ebreak_are_precise_events()
{
    struct SystemCase {
        std::uint32_t instruction;
        rv32::ExceptionCause cause;
        std::uint32_t trap_value;
    };
    constexpr std::array cases{
        SystemCase{
            .instruction = 0x00000073U,
            .cause =
                rv32::ExceptionCause::EnvironmentCallFromMachine,
            .trap_value = 0,
        },
        SystemCase{
            .instruction = 0x00100073U,
            .cause = rv32::ExceptionCause::Breakpoint,
            .trap_value = ProgramBus::base,
        },
    };

    for (const auto& test : cases) {
        ProgramBus bus;
        bus.words[0] = test.instruction;
        rv32::Core core(bus);
        const auto before = core.snapshot();

        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.pc == ProgramBus::base);
        CHECK(result.instruction == test.instruction);
        CHECK(result.trap_value == test.trap_value);
        CHECK(result.bus_fault == rv32::BusFault::None);
        check_precise_machine_trap(
            core.snapshot(),
            before,
            test.cause,
            test.trap_value);
        CHECK(bus.instruction_fetch_count == 1U);
        CHECK(bus.load_count == 0U);
        CHECK(bus.store_count == 0U);
    }
}

} // namespace

int main()
{
    test_reset_initializes_boot_arguments();
    test_register_values_flow_through_complete_steps();
    test_m_extension_flows_through_complete_steps();
    test_misaligned_pc_does_not_change_state();
    test_fetch_fault_does_not_change_state();
    test_illegal_instruction_does_not_change_state();
    test_load_result_flows_into_store();
    test_data_misalignment_does_not_commit();
    test_data_bus_faults_do_not_commit();
    test_control_flow_runs_through_complete_steps();
    test_control_target_misalignment_is_atomic();
    test_fence_retires_through_complete_step();
    test_ecall_and_ebreak_are_precise_events();

    if (failures == 0) {
        std::cout << "All RV32 Core::step tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
