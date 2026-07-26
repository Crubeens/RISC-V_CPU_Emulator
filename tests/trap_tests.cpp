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
    std::array<std::uint32_t, 32> words{};

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
        return 0x1122334455667788ULL;
    }
};

void test_machine_csr_permissions_and_warl_values()
{
    ProgramBus bus;
    rv32::CpuSnapshot state{};
    state.hart_id = 7U;
    rv32::CsrFile csrs(state, bus);

    CHECK(
        csrs.read(
                rv32::csr_address::mstatus,
                rv32::PrivilegeMode::Supervisor)
            .status ==
        rv32::CsrAccessStatus::PrivilegeViolation);
    CHECK(
        csrs.read(
                rv32::csr_address::misa,
                rv32::PrivilegeMode::Machine)
            .value == rv32::machine_isa_value);
    CHECK(
        csrs.read(
                rv32::csr_address::mhartid,
                rv32::PrivilegeMode::Machine)
            .value == 7U);
    CHECK(
        csrs.validate_write(
            rv32::csr_address::mhartid,
            rv32::PrivilegeMode::Machine) ==
        rv32::CsrAccessStatus::ReadOnly);

    CHECK(
        csrs.validate_write(
            rv32::csr_address::mstatus,
            rv32::PrivilegeMode::Machine) ==
        rv32::CsrAccessStatus::Ready);
    csrs.write_validated(
        rv32::csr_address::mstatus,
        rv32::mstatus_bits::mie |
            (0x2U << rv32::mstatus_bits::mpp_shift) |
            0x80000000U);
    CHECK(state.machine_csrs.mstatus == rv32::mstatus_bits::mie);

    csrs.write_validated(
        rv32::csr_address::mtvec,
        ProgramBus::base + 0x43U);
    CHECK(state.machine_csrs.mtvec == ProgramBus::base + 0x40U);
    csrs.write_validated(
        rv32::csr_address::mtvec,
        ProgramBus::base + 0x41U);
    CHECK(state.machine_csrs.mtvec == ProgramBus::base + 0x41U);

    csrs.write_validated(
        rv32::csr_address::mepc,
        ProgramBus::base + 0x23U);
    CHECK(state.machine_csrs.mepc == ProgramBus::base + 0x20U);
    csrs.write_validated(rv32::csr_address::mscratch, 0xA5A5A5A5U);
    csrs.write_validated(rv32::csr_address::mcause, 0x12345678U);
    csrs.write_validated(rv32::csr_address::mtval, 0x89ABCDEFU);
    CHECK(state.machine_csrs.mscratch == 0xA5A5A5A5U);
    CHECK(state.machine_csrs.mcause == 0x12345678U);
    CHECK(state.machine_csrs.mtval == 0x89ABCDEFU);

    csrs.write_validated(rv32::csr_address::misa, 0U);
    CHECK(
        csrs.read(
                rv32::csr_address::misa,
                rv32::PrivilegeMode::Machine)
            .value == rv32::machine_isa_value);
}

void test_machine_trap_entry_and_mret_commit()
{
    ProgramBus bus;
    rv32::CpuSnapshot state{};
    state.pc = ProgramBus::base + 0x20U;
    state.privilege = rv32::PrivilegeMode::Supervisor;
    state.machine_csrs.mstatus = rv32::mstatus_bits::mie;
    state.machine_csrs.mtvec = ProgramBus::base + 0x41U;
    state.registers[5] = 0x12345678U;
    state.instructions_retired = 9U;

    rv32::take_machine_trap(
        state,
        {
            .cause = rv32::ExceptionCause::LoadAccessFault,
            .exception_pc = ProgramBus::base + 0x20U,
            .trap_value = 0x90000000U,
        });

    CHECK(state.pc == ProgramBus::base + 0x40U);
    CHECK(state.privilege == rv32::PrivilegeMode::Machine);
    CHECK(state.machine_csrs.mepc == ProgramBus::base + 0x20U);
    CHECK(
        state.machine_csrs.mcause ==
        static_cast<std::uint32_t>(
            rv32::ExceptionCause::LoadAccessFault));
    CHECK(state.machine_csrs.mtval == 0x90000000U);
    CHECK(
        state.machine_csrs.mstatus ==
        (rv32::mstatus_bits::mpie |
         (static_cast<std::uint32_t>(
              rv32::PrivilegeMode::Supervisor)
          << rv32::mstatus_bits::mpp_shift)));
    CHECK(state.registers[5] == 0x12345678U);
    CHECK(state.instructions_retired == 9U);

    const auto decoded = rv32::decode_instruction(0x30200073U);
    const auto result = rv32::execute_privileged(decoded, state);
    rv32::CsrFile csrs(state, bus);

    CHECK(decoded.kind == rv32::InstructionKind::Mret);
    CHECK(result.ready());
    CHECK(result.pending.next_pc == ProgramBus::base + 0x20U);
    CHECK(
        result.pending.privilege_write.value ==
        rv32::PrivilegeMode::Supervisor);
    CHECK(rv32::commit_pending(state, result.pending, &csrs));
    CHECK(state.pc == ProgramBus::base + 0x20U);
    CHECK(state.privilege == rv32::PrivilegeMode::Supervisor);
    CHECK(
        state.machine_csrs.mstatus ==
        (rv32::mstatus_bits::mie | rv32::mstatus_bits::mpie));
    CHECK(state.instructions_retired == 10U);
}

void test_ecall_handler_returns_to_original_program()
{
    ProgramBus bus;
    bus.words[0] = encode_u(ProgramBus::base, 1U);
    bus.words[1] = encode_i(0x41U, 1U, 0U, 1U, 0x13U);
    bus.words[2] =
        encode_csr(rv32::csr_address::mtvec, 1U, 0x1U, 0U);
    bus.words[3] = 0x00000073U;
    bus.words[4] = encode_i(7U, 0U, 0U, 3U, 0x13U);

    constexpr std::size_t handler_index = 0x40U / 4U;
    bus.words[handler_index] =
        encode_csr(rv32::csr_address::mepc, 0U, 0x2U, 2U);
    bus.words[handler_index + 1U] =
        encode_i(4U, 2U, 0U, 2U, 0x13U);
    bus.words[handler_index + 2U] =
        encode_csr(rv32::csr_address::mepc, 2U, 0x1U, 0U);
    bus.words[handler_index + 3U] = 0x30200073U;

    rv32::Core core(bus);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);

    const auto trap_result = core.step({});
    const auto trapped = core.snapshot();
    CHECK(trap_result.status == rv32::StepStatus::TrapTaken);
    CHECK(trap_result.pc == ProgramBus::base + 12U);
    CHECK(trapped.pc == ProgramBus::base + 0x40U);
    CHECK(trapped.machine_csrs.mtvec == ProgramBus::base + 0x41U);
    CHECK(trapped.machine_csrs.mepc == ProgramBus::base + 12U);
    CHECK(
        trapped.machine_csrs.mcause ==
        static_cast<std::uint32_t>(
            rv32::ExceptionCause::EnvironmentCallFromMachine));
    CHECK(trapped.machine_csrs.mtval == 0U);
    CHECK(trapped.instructions_retired == 3U);

    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    CHECK(core.snapshot().pc == ProgramBus::base + 16U);

    CHECK(core.step({}).status == rv32::StepStatus::Retired);
    const auto returned = core.snapshot();
    CHECK(returned.registers[3] == 7U);
    CHECK(returned.machine_csrs.mepc == ProgramBus::base + 16U);
    CHECK(returned.instructions_retired == 8U);
}

void test_mret_from_user_mode_traps_as_illegal()
{
    ProgramBus bus;
    bus.words[0] = encode_u(ProgramBus::base, 1U);
    bus.words[1] = encode_i(0x41U, 1U, 0U, 2U, 0x13U);
    bus.words[2] =
        encode_csr(rv32::csr_address::mtvec, 2U, 0x1U, 0U);
    bus.words[3] = encode_i(0x20U, 1U, 0U, 2U, 0x13U);
    bus.words[4] =
        encode_csr(rv32::csr_address::mepc, 2U, 0x1U, 0U);
    bus.words[5] = 0x30200073U;
    bus.words[0x20U / 4U] = 0x30200073U;

    rv32::Core core(bus);
    for (std::size_t index = 0; index < 6U; ++index) {
        CHECK(core.step({}).status == rv32::StepStatus::Retired);
    }
    CHECK(core.snapshot().privilege == rv32::PrivilegeMode::User);
    CHECK(core.snapshot().pc == ProgramBus::base + 0x20U);

    const auto result = core.step({});
    const auto state = core.snapshot();
    CHECK(result.status == rv32::StepStatus::TrapTaken);
    CHECK(result.instruction == 0x30200073U);
    CHECK(result.trap_value == 0x30200073U);
    CHECK(state.pc == ProgramBus::base + 0x40U);
    CHECK(state.privilege == rv32::PrivilegeMode::Machine);
    CHECK(state.machine_csrs.mepc == ProgramBus::base + 0x20U);
    CHECK(
        state.machine_csrs.mcause ==
        static_cast<std::uint32_t>(
            rv32::ExceptionCause::IllegalInstruction));
    CHECK(state.machine_csrs.mtval == 0x30200073U);
    CHECK(
        (state.machine_csrs.mstatus & rv32::mstatus_bits::mpp) ==
        0U);
    CHECK(state.instructions_retired == 6U);
}

void test_ecall_cause_depends_on_originating_privilege()
{
    CHECK(
        rv32::environment_call_cause(rv32::PrivilegeMode::User) ==
        rv32::ExceptionCause::EnvironmentCallFromUser);
    CHECK(
        rv32::environment_call_cause(
            rv32::PrivilegeMode::Supervisor) ==
        rv32::ExceptionCause::EnvironmentCallFromSupervisor);
    CHECK(
        rv32::environment_call_cause(rv32::PrivilegeMode::Machine) ==
        rv32::ExceptionCause::EnvironmentCallFromMachine);
}

} // namespace

int main()
{
    test_machine_csr_permissions_and_warl_values();
    test_machine_trap_entry_and_mret_commit();
    test_ecall_handler_returns_to_original_program();
    test_mret_from_user_mode_traps_as_illegal();
    test_ecall_cause_depends_on_originating_privilege();

    if (failures == 0) {
        std::cout << "All machine trap and MRET tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
