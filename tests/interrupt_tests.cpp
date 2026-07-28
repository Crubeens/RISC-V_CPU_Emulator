#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>

#include "rv32/core/core.hpp"
#include "rv32/core/csr.hpp"
#include "rv32/core/decode.hpp"
#include "rv32/core/execute.hpp"
#include "rv32/core/interrupt.hpp"
#include "rv32/core/mmu.hpp"
#include "rv32/core/trap.hpp"
#include "rv32/devices/clint.hpp"
#include "rv32/devices/plic.hpp"
#include "rv32/platform/address_map.hpp"
#include "rv32/platform/machine.hpp"

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
    std::uint32_t opcode = 0x13U) noexcept
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           (opcode & 0x7FU);
}

[[nodiscard]] constexpr std::uint32_t encode_csr(
    rv32::CsrAddress address,
    std::uint32_t source,
    std::uint32_t funct3,
    std::uint32_t rd) noexcept
{
    return (static_cast<std::uint32_t>(address) << 20U) |
           ((source & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x73U;
}

class ProgramBus final : public rv32::CpuBus {
  public:
    static constexpr std::uint32_t base = 0x80000000U;

    std::array<std::uint32_t, 16> words{};
    std::uint32_t instruction_fetch_count{};

    rv32::ReadResult read(
        rv32::PhysAddr address,
        rv32::AccessWidth width,
        rv32::AccessKind kind) override
    {
        if (kind != rv32::AccessKind::InstructionFetch ||
            width != rv32::AccessWidth::HalfWord) {
            return {.fault = rv32::BusFault::Unsupported};
        }
        if (address < base || (address & 0x1U) != 0U) {
            return {.fault = rv32::BusFault::Unmapped};
        }

        const rv32::PhysAddr offset = address - base;
        const rv32::PhysAddr index = offset / 4U;
        if (index >= words.size()) {
            return {.fault = rv32::BusFault::Unmapped};
        }
        ++instruction_fetch_count;
        return {
            .fault = rv32::BusFault::None,
            .value =
                (words[static_cast<std::size_t>(index)] >>
                 (static_cast<unsigned int>(offset & 0x2U) * 8U)) &
                0xFFFFU,
        };
    }

    rv32::BusFault write(
        rv32::PhysAddr,
        rv32::AccessWidth,
        std::uint64_t,
        rv32::AccessKind) override
    {
        return rv32::BusFault::Unsupported;
    }

    rv32::ReadResult load_reserved_word(
        std::uint32_t,
        rv32::PhysAddr) override
    {
        return {.fault = rv32::BusFault::Unsupported};
    }

    rv32::StoreConditionalResult store_conditional_word(
        std::uint32_t,
        rv32::PhysAddr,
        std::uint32_t) override
    {
        return {.fault = rv32::BusFault::Unsupported};
    }

    rv32::AtomicResult atomic_word(
        std::uint32_t,
        rv32::PhysAddr,
        rv32::AmoOperation,
        std::uint32_t) override
    {
        return {.fault = rv32::BusFault::Unsupported};
    }

    [[nodiscard]] std::uint64_t read_time() const noexcept override
    {
        return 0;
    }
};

void test_pending_csr_views_and_exact_read_modify_write()
{
    ProgramBus bus;
    rv32::CpuSnapshot state;
    state.pc = ProgramBus::base;
    state.machine_csrs.mideleg =
        rv32::supported_interrupt_delegation;
    state.machine_csrs.mie =
        rv32::interrupt_bits::machine_timer |
        rv32::interrupt_bits::supervisor_timer |
        rv32::interrupt_bits::supervisor_external;
    state.machine_csrs.mip_software =
        rv32::interrupt_bits::supervisor_timer;
    rv32::sample_interrupt_lines(
        state,
        {
            .machine_timer = true,
            .supervisor_software = true,
            .supervisor_external = true,
        });
    rv32::CsrFile csrs(state, bus);

    const std::uint32_t expected_pending =
        rv32::interrupt_bits::machine_timer |
        rv32::interrupt_bits::supervisor_software |
        rv32::interrupt_bits::supervisor_timer |
        rv32::interrupt_bits::supervisor_external;
    CHECK(
        csrs.read(
                rv32::csr_address::mip,
                rv32::PrivilegeMode::Machine)
            .value == expected_pending);
    CHECK(
        csrs.read(
                rv32::csr_address::sie,
                rv32::PrivilegeMode::Supervisor)
            .value ==
        (rv32::interrupt_bits::supervisor_timer |
         rv32::interrupt_bits::supervisor_external));
    CHECK(
        csrs.read(
                rv32::csr_address::sip,
                rv32::PrivilegeMode::Supervisor)
            .value ==
        (expected_pending & rv32::interrupt_bits::supervisor));

    csrs.write_validated(
        rv32::csr_address::sie,
        rv32::interrupt_bits::supervisor_software |
            rv32::interrupt_bits::supervisor_external);
    CHECK(
        state.machine_csrs.mie ==
        (rv32::interrupt_bits::machine_timer |
         rv32::interrupt_bits::supervisor_software |
         rv32::interrupt_bits::supervisor_external));

    csrs.write_validated(
        rv32::csr_address::mip,
        rv32::interrupt_bits::supervisor_timer);
    CHECK(
        state.machine_csrs.mip_software ==
        rv32::interrupt_bits::supervisor_timer);

    const std::uint32_t mip_read =
        csrs.read(
                rv32::csr_address::mip,
                rv32::PrivilegeMode::Machine)
            .value;
    CHECK(
        csrs.read_for_write(rv32::csr_address::mip, mip_read) ==
        (rv32::interrupt_bits::machine_timer |
         rv32::interrupt_bits::supervisor_timer));

    const std::uint32_t sip_read =
        csrs.read(
                rv32::csr_address::sip,
                rv32::PrivilegeMode::Supervisor)
            .value;
    CHECK(
        csrs.read_for_write(rv32::csr_address::sip, sip_read) ==
        (rv32::interrupt_bits::supervisor_timer |
         rv32::interrupt_bits::supervisor_external));

    csrs.write_validated(
        rv32::csr_address::sip,
        rv32::interrupt_bits::supervisor_software);
    CHECK(
        state.machine_csrs.mip_software ==
        (rv32::interrupt_bits::supervisor_software |
         rv32::interrupt_bits::supervisor_timer));
    csrs.write_validated(rv32::csr_address::sip, 0);
    CHECK(
        state.machine_csrs.mip_software ==
        rv32::interrupt_bits::supervisor_timer);

    // A hardware SEIP line must be returned to rd without being latched into
    // the software-pending bit by an unrelated CSRRC operation.
    state.machine_csrs.mip_software = 0;
    state.machine_csrs.mip_lines =
        rv32::interrupt_bits::supervisor_external;
    const std::uint32_t instruction = encode_csr(
        rv32::csr_address::mip,
        1U,
        0x3U,
        2U);
    const auto decoded = rv32::decode_instruction(instruction);
    const auto result = rv32::execute_csr(
        csrs,
        decoded,
        rv32::PrivilegeMode::Machine,
        state.pc,
        rv32::interrupt_bits::machine_external);
    CHECK(result.ready());
    CHECK(rv32::commit_pending(state, result.pending, &csrs));
    CHECK(
        state.registers[2] ==
        rv32::interrupt_bits::supervisor_external);
    CHECK(state.machine_csrs.mip_software == 0U);
}

void test_privileged_counter_controls_and_warl_csrs()
{
    ProgramBus bus;
    rv32::CpuSnapshot state;
    state.cycle = 0x0123456789ABCDEFULL;
    state.instructions_retired = 0xFEDCBA9876543210ULL;
    state.machine_csrs.mcounteren = 0;
    state.supervisor_csrs.scounteren = 0;
    rv32::CsrFile csrs(state, bus);

    CHECK(
        csrs.read(
                rv32::csr_address::cycle,
                rv32::PrivilegeMode::Machine)
            .value == 0x89ABCDEFU);
    CHECK(
        csrs.read(
                rv32::csr_address::cycle,
                rv32::PrivilegeMode::Supervisor)
            .status == rv32::CsrAccessStatus::PrivilegeViolation);
    CHECK(
        csrs.read(
                rv32::csr_address::cycle,
                rv32::PrivilegeMode::User)
            .status == rv32::CsrAccessStatus::PrivilegeViolation);

    csrs.write_validated(
        rv32::csr_address::mcounteren,
        rv32::supported_counter_enable | (1U << 31U));
    CHECK(
        state.machine_csrs.mcounteren ==
        rv32::supported_counter_enable);
    state.supervisor_csrs.scounteren = 0;
    CHECK(
        csrs.read(
                rv32::csr_address::time,
                rv32::PrivilegeMode::Supervisor)
            .ready());
    CHECK(
        csrs.read(
                rv32::csr_address::time,
                rv32::PrivilegeMode::User)
            .status == rv32::CsrAccessStatus::PrivilegeViolation);
    csrs.write_validated(
        rv32::csr_address::scounteren,
        rv32::supported_counter_enable);
    CHECK(
        csrs.read(
                rv32::csr_address::time,
                rv32::PrivilegeMode::User)
            .ready());

    csrs.write_validated(rv32::csr_address::mcycle, 0x11111111U);
    csrs.write_validated(rv32::csr_address::mcycleh, 0x22222222U);
    CHECK(state.cycle == 0x2222222211111111ULL);
    csrs.write_validated(rv32::csr_address::minstret, 0x33333333U);
    csrs.write_validated(rv32::csr_address::minstreth, 0x44444444U);
    CHECK(state.instructions_retired == 0x4444444433333333ULL);

    csrs.write_validated(rv32::csr_address::satp, 0xFFFFFFFFU);
    CHECK(
        csrs.read(
                rv32::csr_address::satp,
                rv32::PrivilegeMode::Supervisor)
            .value ==
        (rv32::satp_bits::mode |
         rv32::satp_bits::asid |
         rv32::satp_bits::ppn));
    csrs.write_validated(rv32::csr_address::mstatush, 0xFFFFFFFFU);
    CHECK(
        csrs.read(
                rv32::csr_address::mstatush,
                rv32::PrivilegeMode::Machine)
            .value == 0U);
    CHECK(
        csrs.read(
                rv32::csr_address::misa,
                rv32::PrivilegeMode::Machine)
            .value == rv32::machine_isa_value);
}

void test_interrupt_priority_delegation_and_global_enables()
{
    rv32::CpuSnapshot state;
    state.privilege = rv32::PrivilegeMode::Machine;
    state.machine_csrs.mstatus = rv32::mstatus_bits::mie;
    state.machine_csrs.mie = rv32::interrupt_bits::supported;
    rv32::sample_interrupt_lines(
        state,
        {
            .machine_software = true,
            .machine_timer = true,
            .machine_external = true,
            .supervisor_software = true,
            .supervisor_timer = true,
            .supervisor_external = true,
        });

    constexpr std::array priority{
        rv32::InterruptCause::MachineExternal,
        rv32::InterruptCause::MachineSoftware,
        rv32::InterruptCause::MachineTimer,
        rv32::InterruptCause::SupervisorExternal,
        rv32::InterruptCause::SupervisorSoftware,
        rv32::InterruptCause::SupervisorTimer,
    };
    constexpr std::array priority_bits{
        rv32::interrupt_bits::machine_external,
        rv32::interrupt_bits::machine_software,
        rv32::interrupt_bits::machine_timer,
        rv32::interrupt_bits::supervisor_external,
        rv32::interrupt_bits::supervisor_software,
        rv32::interrupt_bits::supervisor_timer,
    };

    std::uint32_t remaining = rv32::interrupt_bits::supported;
    for (std::size_t index = 0; index < priority.size(); ++index) {
        state.machine_csrs.mie = remaining;
        const auto selected = rv32::select_pending_interrupt(state);
        CHECK(selected.pending);
        CHECK(selected.cause == priority[index]);
        CHECK(selected.target == rv32::TrapTarget::Machine);
        remaining &= ~priority_bits[index];
    }
    state.machine_csrs.mie = 0;
    CHECK(!rv32::select_pending_interrupt(state).pending);

    state = {};
    state.privilege = rv32::PrivilegeMode::Machine;
    state.machine_csrs.mie = rv32::interrupt_bits::machine_timer;
    rv32::sample_interrupt_lines(
        state,
        {.machine_timer = true});
    CHECK(rv32::interrupt_wake_requested(state));
    CHECK(!rv32::select_pending_interrupt(state).pending);

    state.privilege = rv32::PrivilegeMode::User;
    auto selected = rv32::select_pending_interrupt(state);
    CHECK(selected.pending);
    CHECK(selected.target == rv32::TrapTarget::Machine);

    state = {};
    state.privilege = rv32::PrivilegeMode::User;
    state.machine_csrs.mie =
        rv32::interrupt_bits::supervisor_external;
    state.machine_csrs.mideleg =
        rv32::interrupt_bits::supervisor_external;
    rv32::sample_interrupt_lines(
        state,
        {.supervisor_external = true});
    selected = rv32::select_pending_interrupt(state);
    CHECK(selected.pending);
    CHECK(
        selected.cause ==
        rv32::InterruptCause::SupervisorExternal);
    CHECK(selected.target == rv32::TrapTarget::Supervisor);

    state.privilege = rv32::PrivilegeMode::Supervisor;
    state.machine_csrs.mstatus = 0;
    CHECK(!rv32::select_pending_interrupt(state).pending);
    state.machine_csrs.mstatus = rv32::mstatus_bits::sie;
    CHECK(rv32::select_pending_interrupt(state).pending);

    state.privilege = rv32::PrivilegeMode::Machine;
    state.machine_csrs.mstatus =
        rv32::mstatus_bits::mie | rv32::mstatus_bits::sie;
    CHECK(!rv32::select_pending_interrupt(state).pending);
}

void test_vectored_interrupt_trap_state()
{
    rv32::CpuSnapshot machine_state;
    machine_state.pc = 0x80000100U;
    machine_state.privilege = rv32::PrivilegeMode::User;
    machine_state.registers[5] = 0xA5A5A5A5U;
    machine_state.machine_csrs.mstatus = rv32::mstatus_bits::mie;
    machine_state.machine_csrs.mtvec = 0x00001001U;
    machine_state.cycle = 17;
    machine_state.instructions_retired = 9;

    rv32::take_interrupt_trap(
        machine_state,
        {
            .cause = rv32::InterruptCause::MachineExternal,
            .interrupted_pc = machine_state.pc,
        },
        rv32::TrapTarget::Machine);
    CHECK(machine_state.pc == 0x0000102CU);
    CHECK(machine_state.privilege == rv32::PrivilegeMode::Machine);
    CHECK(machine_state.machine_csrs.mepc == 0x80000100U);
    CHECK(machine_state.machine_csrs.mcause == 0x8000000BU);
    CHECK(machine_state.machine_csrs.mtval == 0U);
    CHECK(
        (machine_state.machine_csrs.mstatus &
         rv32::mstatus_bits::mie) == 0U);
    CHECK(
        (machine_state.machine_csrs.mstatus &
         rv32::mstatus_bits::mpie) != 0U);
    CHECK(machine_state.registers[5] == 0xA5A5A5A5U);
    CHECK(machine_state.cycle == 17U);
    CHECK(machine_state.instructions_retired == 9U);

    rv32::CpuSnapshot supervisor_state;
    supervisor_state.pc = 0x80000200U;
    supervisor_state.privilege = rv32::PrivilegeMode::Supervisor;
    supervisor_state.machine_csrs.mstatus = rv32::mstatus_bits::sie;
    supervisor_state.supervisor_csrs.stvec = 0x00002001U;
    supervisor_state.instructions_retired = 12;

    rv32::take_interrupt_trap(
        supervisor_state,
        {
            .cause = rv32::InterruptCause::SupervisorExternal,
            .interrupted_pc = supervisor_state.pc,
        },
        rv32::TrapTarget::Supervisor);
    CHECK(supervisor_state.pc == 0x00002024U);
    CHECK(
        supervisor_state.privilege ==
        rv32::PrivilegeMode::Supervisor);
    CHECK(supervisor_state.supervisor_csrs.sepc == 0x80000200U);
    CHECK(
        supervisor_state.supervisor_csrs.scause ==
        0x80000009U);
    CHECK(supervisor_state.supervisor_csrs.stval == 0U);
    CHECK(
        (supervisor_state.machine_csrs.mstatus &
         rv32::mstatus_bits::sie) == 0U);
    CHECK(
        (supervisor_state.machine_csrs.mstatus &
         rv32::mstatus_bits::spie) != 0U);
    CHECK(
        (supervisor_state.machine_csrs.mstatus &
         rv32::mstatus_bits::spp) != 0U);
    CHECK(supervisor_state.instructions_retired == 12U);
}

void test_wfi_stall_wake_and_trap()
{
    {
        ProgramBus bus;
        bus.words[0] = encode_i(0x80U, 0U, 0U, 1U);
        bus.words[1] = encode_csr(
            rv32::csr_address::mie,
            1U,
            0x1U,
            0U);
        bus.words[2] = 0x10500073U; // wfi
        bus.words[3] = encode_i(7U, 0U, 0U, 2U);
        rv32::Core core(bus);

        CHECK(core.step({}).status == rv32::StepStatus::Retired);
        CHECK(core.step({}).status == rv32::StepStatus::Retired);
        const auto wfi = core.step({});
        CHECK(wfi.status == rv32::StepStatus::WaitingForInterrupt);
        CHECK(wfi.instruction == 0x10500073U);
        CHECK(core.snapshot().pc == ProgramBus::base + 12U);
        CHECK(core.snapshot().instructions_retired == 3U);
        CHECK(bus.instruction_fetch_count == 6U);

        const auto stalled = core.step({});
        CHECK(
            stalled.status ==
            rv32::StepStatus::WaitingForInterrupt);
        CHECK(stalled.pc == ProgramBus::base + 12U);
        CHECK(bus.instruction_fetch_count == 6U);
        CHECK(core.snapshot().instructions_retired == 3U);

        const auto woke = core.step({.machine_timer = true});
        CHECK(woke.status == rv32::StepStatus::Retired);
        CHECK(core.snapshot().registers[2] == 7U);
        CHECK(core.snapshot().pc == ProgramBus::base + 16U);
        CHECK(core.snapshot().instructions_retired == 4U);
        CHECK(bus.instruction_fetch_count == 8U);
    }

    {
        ProgramBus bus;
        bus.words[0] = encode_i(0x80U, 0U, 0U, 1U);
        bus.words[1] = encode_csr(
            rv32::csr_address::mie,
            1U,
            0x1U,
            0U);
        bus.words[2] = encode_csr(
            rv32::csr_address::mstatus,
            8U,
            0x6U,
            0U);
        bus.words[3] = 0x10500073U; // wfi
        rv32::Core core(bus);

        CHECK(core.step({}).status == rv32::StepStatus::Retired);
        CHECK(core.step({}).status == rv32::StepStatus::Retired);
        CHECK(core.step({}).status == rv32::StepStatus::Retired);
        CHECK(
            core.step({}).status ==
            rv32::StepStatus::WaitingForInterrupt);
        CHECK(core.snapshot().pc == ProgramBus::base + 16U);
        CHECK(core.snapshot().instructions_retired == 4U);

        const auto trapped =
            core.step({.machine_timer = true});
        CHECK(trapped.status == rv32::StepStatus::TrapTaken);
        CHECK(trapped.pc == ProgramBus::base + 16U);
        CHECK(trapped.instruction == 0U);
        CHECK(core.snapshot().machine_csrs.mepc ==
              ProgramBus::base + 16U);
        CHECK(core.snapshot().machine_csrs.mcause == 0x80000007U);
        CHECK(core.snapshot().instructions_retired == 4U);
        CHECK(bus.instruction_fetch_count == 8U);
    }
}

void test_wfi_privilege_rules()
{
    const auto decoded = rv32::decode_instruction(0x10500073U);
    CHECK(decoded.kind == rv32::InstructionKind::Wfi);

    rv32::CpuSnapshot state;
    state.pc = ProgramBus::base;
    state.privilege = rv32::PrivilegeMode::User;
    CHECK(
        rv32::execute_privileged(decoded, state).status ==
        rv32::PrivilegedExecutionStatus::IllegalInstruction);

    state.privilege = rv32::PrivilegeMode::Supervisor;
    state.machine_csrs.mstatus = rv32::mstatus_bits::tw;
    CHECK(
        rv32::execute_privileged(decoded, state).status ==
        rv32::PrivilegedExecutionStatus::IllegalInstruction);

    state.machine_csrs.mstatus = 0;
    CHECK(rv32::execute_privileged(decoded, state).ready());
    state.privilege = rv32::PrivilegeMode::Machine;
    state.machine_csrs.mstatus = rv32::mstatus_bits::tw;
    CHECK(rv32::execute_privileged(decoded, state).ready());
}

template <std::size_t Size>
void load_machine_program(
    rv32::platform::Machine& machine,
    const std::array<std::uint32_t, Size>& program)
{
    for (std::size_t index = 0; index < program.size(); ++index) {
        CHECK(
            machine.bus().dma_write(
                rv32::platform::address_map::dram_base +
                    static_cast<rv32::PhysAddr>(index * 4U),
                rv32::AccessWidth::Word,
                program[index]) == rv32::BusFault::None);
    }
}

void test_clint_drives_machine_timer_trap()
{
    rv32::platform::MachineConfig config;
    config.ram_size = 0x1000U;
    config.virtual_disk_size = 512U;
    config.enable_framebuffer = false;
    rv32::platform::Machine machine(config);

    constexpr std::array program{
        encode_i(0x80U, 0U, 0U, 1U),
        encode_csr(rv32::csr_address::mie, 1U, 0x1U, 0U),
        encode_csr(rv32::csr_address::mstatus, 8U, 0x6U, 0U),
        encode_i(1U, 0U, 0U, 2U),
    };
    load_machine_program(machine, program);

    CHECK(machine.step().status == rv32::StepStatus::Retired);
    CHECK(machine.step().status == rv32::StepStatus::Retired);
    CHECK(machine.step().status == rv32::StepStatus::Retired);
    const std::uint64_t compare = machine.clint().mtime() + 1U;
    CHECK(
        machine.bus().write(
            rv32::platform::address_map::clint_base + 0x4000U,
            rv32::AccessWidth::DoubleWord,
            compare,
            rv32::AccessKind::Store) == rv32::BusFault::None);

    const auto result = machine.step();
    CHECK(result.status == rv32::StepStatus::TrapTaken);
    CHECK(machine.irq_lines().machine_timer);
    CHECK(
        machine.core().snapshot().machine_csrs.mepc ==
        rv32::platform::address_map::default_reset_pc + 12U);
    CHECK(
        machine.core().snapshot().machine_csrs.mcause ==
        0x80000007U);
    CHECK(machine.core().snapshot().instructions_retired == 3U);
}

void test_plic_drives_machine_external_trap()
{
    rv32::platform::MachineConfig config;
    config.ram_size = 0x1000U;
    config.virtual_disk_size = 512U;
    config.enable_framebuffer = false;
    rv32::platform::Machine machine(config);

    constexpr std::array program{
        encode_i(0x800U, 0U, 0U, 1U),
        encode_csr(rv32::csr_address::mie, 1U, 0x1U, 0U),
        encode_csr(rv32::csr_address::mstatus, 8U, 0x6U, 0U),
        encode_i(1U, 0U, 0U, 2U),
    };
    load_machine_program(machine, program);

    CHECK(machine.step().status == rv32::StepStatus::Retired);
    CHECK(machine.step().status == rv32::StepStatus::Retired);
    CHECK(machine.step().status == rv32::StepStatus::Retired);

    constexpr std::uint32_t source = 2U;
    const rv32::PhysAddr plic_base =
        rv32::platform::address_map::plic_base;
    CHECK(
        machine.bus().write(
            plic_base + source * 4U,
            rv32::AccessWidth::Word,
            3U,
            rv32::AccessKind::Store) == rv32::BusFault::None);
    CHECK(
        machine.bus().write(
            plic_base + 0x2000U,
            rv32::AccessWidth::Word,
            1U << source,
            rv32::AccessKind::Store) == rv32::BusFault::None);
    CHECK(
        machine.bus().write(
            plic_base + 0x200000U,
            rv32::AccessWidth::Word,
            0U,
            rv32::AccessKind::Store) == rv32::BusFault::None);
    machine.plic().set_source_level(source, true);

    const auto result = machine.step();
    CHECK(result.status == rv32::StepStatus::TrapTaken);
    CHECK(machine.irq_lines().machine_external);
    CHECK(
        machine.core().snapshot().machine_csrs.mepc ==
        rv32::platform::address_map::default_reset_pc + 12U);
    CHECK(
        machine.core().snapshot().machine_csrs.mcause ==
        0x8000000BU);
    CHECK(machine.core().snapshot().instructions_retired == 3U);
}

} // namespace

int main()
{
    test_pending_csr_views_and_exact_read_modify_write();
    test_privileged_counter_controls_and_warl_csrs();
    test_interrupt_priority_delegation_and_global_enables();
    test_vectored_interrupt_trap_state();
    test_wfi_stall_wake_and_trap();
    test_wfi_privilege_rules();
    test_clint_drives_machine_timer_trap();
    test_plic_drives_machine_external_trap();

    if (failures == 0) {
        std::cout
            << "All privileged interrupt, WFI, CLINT, and PLIC tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
