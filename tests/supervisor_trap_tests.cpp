#include <array>
#include <cstdint>
#include <iostream>

#include "rv32/core/core.hpp"
#include "rv32/core/csr.hpp"
#include "rv32/core/decode.hpp"
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

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t immediate,
    std::uint32_t rd) noexcept
{
    return (immediate & 0xFFFFF000U) |
           ((rd & 0x1FU) << 7U) |
           0x37U;
}

[[nodiscard]] constexpr std::uint32_t encode_csr(
    rv32::CsrAddress address,
    std::uint32_t source,
    std::uint32_t funct3,
    std::uint32_t rd) noexcept
{
    return encode_i(address, source, funct3, rd, 0x73U);
}

class ProgramBus final : public rv32::CpuBus {
  public:
    static constexpr std::uint32_t base = 0x80000000U;
    std::array<std::uint32_t, 64> words{};

    rv32::ReadResult read(
        rv32::PhysAddr address,
        rv32::AccessWidth width,
        rv32::AccessKind kind) override
    {
        if (kind != rv32::AccessKind::InstructionFetch ||
            width != rv32::AccessWidth::Word ||
            address < base) {
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

    std::uint64_t read_time() const noexcept override
    {
        return 0;
    }
};

void test_supervisor_csrs_and_delegation_masks()
{
    ProgramBus bus;
    rv32::CpuSnapshot state{};
    state.machine_csrs.mstatus =
        rv32::mstatus_bits::mie |
        rv32::mstatus_bits::mpie |
        rv32::mstatus_bits::mpp |
        rv32::mstatus_bits::sie |
        rv32::mstatus_bits::tsr;
    rv32::CsrFile csrs(state, bus);

    const auto sstatus = csrs.read(
        rv32::csr_address::sstatus,
        rv32::PrivilegeMode::Supervisor);
    CHECK(sstatus.ready());
    CHECK(sstatus.value == rv32::mstatus_bits::sie);
    CHECK(
        csrs.read(
                rv32::csr_address::sstatus,
                rv32::PrivilegeMode::User)
            .status ==
        rv32::CsrAccessStatus::PrivilegeViolation);

    csrs.write_validated(
        rv32::csr_address::sstatus,
        rv32::mstatus_bits::spie | rv32::mstatus_bits::spp);
    CHECK(
        (state.machine_csrs.mstatus &
         rv32::mstatus_bits::supervisor_view) ==
        (rv32::mstatus_bits::spie | rv32::mstatus_bits::spp));
    CHECK(
        (state.machine_csrs.mstatus & rv32::mstatus_bits::mie) !=
        0U);
    CHECK(
        (state.machine_csrs.mstatus & rv32::mstatus_bits::mpp) ==
        rv32::mstatus_bits::mpp);
    CHECK(
        (state.machine_csrs.mstatus & rv32::mstatus_bits::tsr) !=
        0U);

    csrs.write_validated(
        rv32::csr_address::stvec,
        ProgramBus::base + 0x83U);
    csrs.write_validated(
        rv32::csr_address::sepc,
        ProgramBus::base + 0x43U);
    csrs.write_validated(rv32::csr_address::sscratch, 0x11112222U);
    csrs.write_validated(rv32::csr_address::scause, 0x80000009U);
    csrs.write_validated(rv32::csr_address::stval, 0x33334444U);
    CHECK(state.supervisor_csrs.stvec == ProgramBus::base + 0x80U);
    CHECK(state.supervisor_csrs.sepc == ProgramBus::base + 0x40U);
    CHECK(state.supervisor_csrs.sscratch == 0x11112222U);
    CHECK(state.supervisor_csrs.scause == 0x80000009U);
    CHECK(state.supervisor_csrs.stval == 0x33334444U);

    csrs.write_validated(rv32::csr_address::medeleg, 0xFFFFFFFFU);
    csrs.write_validated(rv32::csr_address::mideleg, 0xFFFFFFFFU);
    csrs.write_validated(rv32::csr_address::medelegh, 0xFFFFFFFFU);
    CHECK(
        state.machine_csrs.medeleg ==
        rv32::supported_exception_delegation);
    CHECK(
        (state.machine_csrs.medeleg & (1U << 11U)) == 0U);
    CHECK(
        state.machine_csrs.mideleg ==
        rv32::supported_interrupt_delegation);
    CHECK(
        csrs.read(
                rv32::csr_address::medelegh,
                rv32::PrivilegeMode::Machine)
            .value == 0U);
    CHECK(
        csrs.read(
                rv32::csr_address::medeleg,
                rv32::PrivilegeMode::Supervisor)
            .status ==
        rv32::CsrAccessStatus::PrivilegeViolation);
}

void test_delegated_user_trap_and_sret_state_stack()
{
    ProgramBus bus;
    rv32::CpuSnapshot state{};
    state.pc = ProgramBus::base + 0x20U;
    state.privilege = rv32::PrivilegeMode::User;
    state.machine_csrs.medeleg =
        1U << static_cast<std::uint32_t>(
            rv32::ExceptionCause::IllegalInstruction);
    state.machine_csrs.mstatus = rv32::mstatus_bits::sie;
    state.supervisor_csrs.stvec = ProgramBus::base + 0x101U;
    state.registers[6] = 0xA5A5A5A5U;
    state.instructions_retired = 4U;

    const auto target = rv32::take_trap(
        state,
        {
            .cause = rv32::ExceptionCause::IllegalInstruction,
            .exception_pc = ProgramBus::base + 0x20U,
            .trap_value = 0xFFFFFFFFU,
        });

    CHECK(target == rv32::TrapTarget::Supervisor);
    CHECK(state.pc == ProgramBus::base + 0x100U);
    CHECK(state.privilege == rv32::PrivilegeMode::Supervisor);
    CHECK(state.supervisor_csrs.sepc == ProgramBus::base + 0x20U);
    CHECK(
        state.supervisor_csrs.scause ==
        static_cast<std::uint32_t>(
            rv32::ExceptionCause::IllegalInstruction));
    CHECK(state.supervisor_csrs.stval == 0xFFFFFFFFU);
    CHECK(
        state.machine_csrs.mstatus == rv32::mstatus_bits::spie);
    CHECK(state.registers[6] == 0xA5A5A5A5U);
    CHECK(state.instructions_retired == 4U);

    const auto decoded = rv32::decode_instruction(0x10200073U);
    const auto result = rv32::execute_privileged(decoded, state);
    rv32::CsrFile csrs(state, bus);
    CHECK(decoded.kind == rv32::InstructionKind::Sret);
    CHECK(result.ready());
    CHECK(rv32::commit_pending(state, result.pending, &csrs));
    CHECK(state.pc == ProgramBus::base + 0x20U);
    CHECK(state.privilege == rv32::PrivilegeMode::User);
    CHECK(
        state.machine_csrs.mstatus ==
        (rv32::mstatus_bits::sie | rv32::mstatus_bits::spie));
    CHECK(state.instructions_retired == 5U);
}

void test_delegation_never_lowers_machine_traps()
{
    {
        rv32::CpuSnapshot state{};
        state.pc = ProgramBus::base;
        state.privilege = rv32::PrivilegeMode::User;
        state.machine_csrs.mtvec = ProgramBus::base + 0x80U;
        state.supervisor_csrs.stvec = ProgramBus::base + 0x100U;

        const auto target = rv32::take_trap(
            state,
            {
                .cause = rv32::ExceptionCause::LoadAccessFault,
                .exception_pc = ProgramBus::base,
                .trap_value = 0x90000000U,
            });
        CHECK(target == rv32::TrapTarget::Machine);
        CHECK(state.pc == ProgramBus::base + 0x80U);
        CHECK(state.privilege == rv32::PrivilegeMode::Machine);
        CHECK(state.machine_csrs.mepc == ProgramBus::base);
        CHECK(state.supervisor_csrs.sepc == 0U);
    }

    {
        rv32::CpuSnapshot state{};
        state.pc = ProgramBus::base;
        state.privilege = rv32::PrivilegeMode::Machine;
        state.machine_csrs.medeleg =
            1U << static_cast<std::uint32_t>(
                rv32::ExceptionCause::IllegalInstruction);
        state.machine_csrs.mtvec = ProgramBus::base + 0x80U;
        state.supervisor_csrs.stvec = ProgramBus::base + 0x100U;

        const auto target = rv32::take_trap(
            state,
            {
                .cause = rv32::ExceptionCause::IllegalInstruction,
                .exception_pc = ProgramBus::base,
                .trap_value = 0xFFFFFFFFU,
            });
        CHECK(target == rv32::TrapTarget::Machine);
        CHECK(state.pc == ProgramBus::base + 0x80U);
        CHECK(state.privilege == rv32::PrivilegeMode::Machine);
        CHECK(
            (state.machine_csrs.mstatus &
             rv32::mstatus_bits::mpp) ==
            rv32::mstatus_bits::mpp);
        CHECK(state.supervisor_csrs.sepc == 0U);
    }
}

void test_horizontal_supervisor_trap_returns_to_supervisor()
{
    ProgramBus bus;
    rv32::CpuSnapshot state{};
    state.pc = ProgramBus::base + 0x20U;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    state.machine_csrs.medeleg =
        1U << static_cast<std::uint32_t>(
            rv32::ExceptionCause::Breakpoint);
    state.supervisor_csrs.stvec = ProgramBus::base + 0x100U;

    CHECK(
        rv32::take_trap(
            state,
            {
                .cause = rv32::ExceptionCause::Breakpoint,
                .exception_pc = ProgramBus::base + 0x20U,
                .trap_value = ProgramBus::base + 0x20U,
            }) == rv32::TrapTarget::Supervisor);
    CHECK(
        (state.machine_csrs.mstatus & rv32::mstatus_bits::spp) !=
        0U);

    const auto result = rv32::execute_privileged(
        rv32::decode_instruction(0x10200073U),
        state);
    rv32::CsrFile csrs(state, bus);
    CHECK(result.ready());
    CHECK(rv32::commit_pending(state, result.pending, &csrs));
    CHECK(state.privilege == rv32::PrivilegeMode::Supervisor);
    CHECK(state.pc == ProgramBus::base + 0x20U);
    CHECK(
        (state.machine_csrs.mstatus & rv32::mstatus_bits::spp) ==
        0U);
}

void test_sret_privilege_and_tsr_rules()
{
    ProgramBus bus;
    const auto decoded = rv32::decode_instruction(0x10200073U);

    {
        rv32::CpuSnapshot state{};
        state.privilege = rv32::PrivilegeMode::User;
        const auto result = rv32::execute_privileged(decoded, state);
        CHECK(
            result.status ==
            rv32::PrivilegedExecutionStatus::IllegalInstruction);
    }

    {
        rv32::CpuSnapshot state{};
        state.privilege = rv32::PrivilegeMode::Supervisor;
        state.machine_csrs.mstatus = rv32::mstatus_bits::tsr;
        const auto result = rv32::execute_privileged(decoded, state);
        CHECK(
            result.status ==
            rv32::PrivilegedExecutionStatus::IllegalInstruction);
    }

    {
        rv32::CpuSnapshot state{};
        state.pc = ProgramBus::base;
        state.privilege = rv32::PrivilegeMode::Machine;
        state.machine_csrs.mstatus =
            rv32::mstatus_bits::tsr |
            rv32::mstatus_bits::spie |
            rv32::mstatus_bits::spp;
        state.supervisor_csrs.sepc = ProgramBus::base + 0x40U;

        const auto result = rv32::execute_privileged(decoded, state);
        rv32::CsrFile csrs(state, bus);
        CHECK(result.ready());
        CHECK(rv32::commit_pending(state, result.pending, &csrs));
        CHECK(state.privilege == rv32::PrivilegeMode::Supervisor);
        CHECK(state.pc == ProgramBus::base + 0x40U);
        CHECK(
            (state.machine_csrs.mstatus & rv32::mstatus_bits::tsr) !=
            0U);
        CHECK(
            (state.machine_csrs.mstatus &
             rv32::mstatus_bits::supervisor_view) ==
            (rv32::mstatus_bits::sie |
             rv32::mstatus_bits::spie));
    }
}

void test_user_ecall_runs_supervisor_handler_and_returns()
{
    ProgramBus bus;
    bus.words[0] = encode_u(ProgramBus::base, 1U);
    bus.words[1] = encode_i(0x81U, 1U, 0U, 2U, 0x13U);
    bus.words[2] =
        encode_csr(rv32::csr_address::stvec, 2U, 0x1U, 0U);
    bus.words[3] = encode_i(1U << 8U, 0U, 0U, 2U, 0x13U);
    bus.words[4] =
        encode_csr(rv32::csr_address::medeleg, 2U, 0x1U, 0U);
    bus.words[5] = encode_i(0x40U, 1U, 0U, 2U, 0x13U);
    bus.words[6] =
        encode_csr(rv32::csr_address::mepc, 2U, 0x1U, 0U);
    bus.words[7] = 0x30200073U;

    constexpr std::size_t user_index = 0x40U / 4U;
    bus.words[user_index] = 0x00000073U;
    bus.words[user_index + 1U] =
        encode_i(9U, 0U, 0U, 5U, 0x13U);

    constexpr std::size_t handler_index = 0x80U / 4U;
    bus.words[handler_index] =
        encode_csr(rv32::csr_address::sepc, 0U, 0x2U, 3U);
    bus.words[handler_index + 1U] =
        encode_i(4U, 3U, 0U, 3U, 0x13U);
    bus.words[handler_index + 2U] =
        encode_csr(rv32::csr_address::sepc, 3U, 0x1U, 0U);
    bus.words[handler_index + 3U] = 0x10200073U;

    rv32::Core core(bus);
    for (std::size_t index = 0; index < 8U; ++index) {
        CHECK(core.step({}).status == rv32::StepStatus::Retired);
    }
    CHECK(core.snapshot().privilege == rv32::PrivilegeMode::User);
    CHECK(core.snapshot().pc == ProgramBus::base + 0x40U);

    const auto trap_result = core.step({});
    const auto trapped = core.snapshot();
    CHECK(trap_result.status == rv32::StepStatus::TrapTaken);
    CHECK(trap_result.pc == ProgramBus::base + 0x40U);
    CHECK(trapped.privilege == rv32::PrivilegeMode::Supervisor);
    CHECK(trapped.pc == ProgramBus::base + 0x80U);
    CHECK(trapped.supervisor_csrs.stvec == ProgramBus::base + 0x81U);
    CHECK(trapped.supervisor_csrs.sepc == ProgramBus::base + 0x40U);
    CHECK(
        trapped.supervisor_csrs.scause ==
        static_cast<std::uint32_t>(
            rv32::ExceptionCause::EnvironmentCallFromUser));
    CHECK(trapped.supervisor_csrs.stval == 0U);
    CHECK(trapped.machine_csrs.mcause == 0U);
    CHECK(trapped.instructions_retired == 8U);

    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.snapshot().privilege == rv32::PrivilegeMode::User);
    CHECK(core.snapshot().pc == ProgramBus::base + 0x44U);

    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    const auto returned = core.snapshot();
    CHECK(returned.registers[5] == 9U);
    CHECK(returned.privilege == rv32::PrivilegeMode::User);
    CHECK(returned.supervisor_csrs.sepc == ProgramBus::base + 0x44U);
    CHECK(returned.instructions_retired == 13U);
}

} // namespace

int main()
{
    test_supervisor_csrs_and_delegation_masks();
    test_delegated_user_trap_and_sret_state_stack();
    test_delegation_never_lowers_machine_traps();
    test_horizontal_supervisor_trap_returns_to_supervisor();
    test_sret_privilege_and_tsr_rules();
    test_user_ecall_runs_supervisor_handler_and_returns();

    if (failures == 0) {
        std::cout
            << "All supervisor trap delegation and SRET tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
