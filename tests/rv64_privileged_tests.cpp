#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

#include "rv/common/bus.hpp"
#include "rv64/core/core.hpp"
#include "rv64/core/csr.hpp"
#include "rv64/core/decode.hpp"
#include "rv64/core/interrupt.hpp"
#include "rv64/core/trap.hpp"

namespace {

constexpr std::uint64_t base = 0x80000000ULL;
int failures = 0;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << ": "     \
                      << #condition << '\n';                                 \
            ++failures;                                                      \
        }                                                                    \
    } while (false)

class TestBus final : public rv::CpuBus {
  public:
    [[nodiscard]] rv::ReadResult read(
        rv::PhysAddr address,
        rv::AccessWidth width,
        rv::AccessKind kind) override
    {
        static_cast<void>(kind);
        const std::uint64_t count = rv::width_bytes(width);
        if ((address & (count - 1U)) != 0U) {
            return {.fault = rv::BusFault::Misaligned};
        }
        if (address < base || count > bytes_.size() ||
            address - base > bytes_.size() - count) {
            return {.fault = rv::BusFault::Unmapped};
        }
        const std::size_t offset =
            static_cast<std::size_t>(address - base);
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < count; ++index) {
            value |= static_cast<std::uint64_t>(bytes_[offset + index])
                     << (index * 8U);
        }
        return {.fault = rv::BusFault::None, .value = value};
    }

    [[nodiscard]] rv::BusFault write(
        rv::PhysAddr address,
        rv::AccessWidth width,
        std::uint64_t value,
        rv::AccessKind kind) override
    {
        static_cast<void>(address);
        static_cast<void>(width);
        static_cast<void>(value);
        static_cast<void>(kind);
        return rv::BusFault::Unsupported;
    }

    [[nodiscard]] rv::ReadResult load_reserved_word(
        std::uint32_t hart_id,
        rv::PhysAddr address) override
    {
        static_cast<void>(hart_id);
        static_cast<void>(address);
        return {.fault = rv::BusFault::Unsupported};
    }

    [[nodiscard]] rv::StoreConditionalResult store_conditional_word(
        std::uint32_t hart_id,
        rv::PhysAddr address,
        std::uint32_t value) override
    {
        static_cast<void>(hart_id);
        static_cast<void>(address);
        static_cast<void>(value);
        return {.fault = rv::BusFault::Unsupported};
    }

    [[nodiscard]] rv::AtomicResult atomic_word(
        std::uint32_t hart_id,
        rv::PhysAddr address,
        rv::AmoOperation operation,
        std::uint32_t operand) override
    {
        static_cast<void>(hart_id);
        static_cast<void>(address);
        static_cast<void>(operation);
        static_cast<void>(operand);
        return {.fault = rv::BusFault::Unsupported};
    }

    [[nodiscard]] std::uint64_t read_time() const noexcept override
    {
        return 0x123456789ABCDEF0ULL;
    }

    void load_program(std::span<const std::uint32_t> program)
    {
        bytes_.fill(0);
        for (std::size_t word = 0; word < program.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                bytes_[word * 4U + byte] =
                    static_cast<std::uint8_t>(
                        program[word] >> (byte * 8U));
            }
        }
    }

  private:
    std::array<std::uint8_t, 4096> bytes_{};
};

[[nodiscard]] constexpr std::uint32_t encode_i(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd)
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x13U;
}

[[nodiscard]] constexpr std::uint32_t encode_csr(
    std::uint16_t csr,
    std::uint32_t source,
    std::uint32_t funct3,
    std::uint32_t rd)
{
    return (static_cast<std::uint32_t>(csr) << 20U) |
           ((source & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x73U;
}

void test_privileged_decode()
{
    using K = rv64::InstructionKind;
    const auto csrrw = rv64::decode_instruction(
        encode_csr(rv64::csr_address::mstatus, 2U, 1U, 1U));
    CHECK(csrrw.kind == K::Csrrw);
    CHECK(csrrw.csr == rv64::csr_address::mstatus);
    CHECK(csrrw.rs1 == 2U);
    CHECK(rv64::decode_instruction(
        encode_csr(0x340U, 3U, 2U, 4U)).kind == K::Csrrs);
    CHECK(rv64::decode_instruction(
        encode_csr(0x340U, 3U, 3U, 4U)).kind == K::Csrrc);
    const auto immediate = rv64::decode_instruction(
        encode_csr(0x340U, 31U, 5U, 4U));
    CHECK(immediate.kind == K::Csrrwi);
    CHECK(immediate.immediate == 31U);
    CHECK(rv64::decode_instruction(
        encode_csr(0x340U, 3U, 6U, 4U)).kind == K::Csrrsi);
    CHECK(rv64::decode_instruction(
        encode_csr(0x340U, 3U, 7U, 4U)).kind == K::Csrrci);
    CHECK(rv64::decode_instruction(0x30200073U).kind == K::Mret);
    CHECK(rv64::decode_instruction(0x10200073U).kind == K::Sret);
    CHECK(rv64::decode_instruction(0x10500073U).kind == K::Wfi);
    CHECK(rv64::decode_instruction(0x12000073U).kind == K::SfenceVma);
}

void test_csr_width_aliases_and_warl()
{
    TestBus bus;
    rv64::CpuSnapshot state;
    state.hart_id = 9U;
    state.machine_csrs.mstatus = rv64::sanitize_mstatus(0);
    rv64::CsrFile csrs(state, bus);

    CHECK(
        csrs.read(
            rv64::csr_address::misa,
            rv64::PrivilegeMode::Machine).value ==
        0x8000000000141105ULL);
    CHECK(
        csrs.read(
            rv64::csr_address::mhartid,
            rv64::PrivilegeMode::Machine).value == 9U);
    CHECK(
        csrs.read(
            rv64::csr_address::time,
            rv64::PrivilegeMode::Machine).value ==
        0x123456789ABCDEF0ULL);

    csrs.write_validated(
        rv64::csr_address::mstatus,
        ~std::uint64_t{0});
    const std::uint64_t mstatus =
        csrs.read(
            rv64::csr_address::mstatus,
            rv64::PrivilegeMode::Machine).value;
    CHECK((mstatus & rv64::mstatus_bits::fixed) ==
          rv64::mstatus_bits::fixed);
    CHECK(
        (mstatus & rv64::mstatus_bits::mpp) ==
        rv64::mstatus_bits::mpp);

    csrs.write_validated(rv64::csr_address::mstatus, 2ULL << 11U);
    CHECK(
        (state.machine_csrs.mstatus & rv64::mstatus_bits::mpp) ==
        0U);
    csrs.write_validated(rv64::csr_address::mtvec, base | 3U);
    CHECK(state.machine_csrs.mtvec == base);
    csrs.write_validated(rv64::csr_address::stvec, base | 1U);
    CHECK(state.supervisor_csrs.stvec == (base | 1U));
    csrs.write_validated(rv64::csr_address::mepc, base + 3U);
    CHECK(state.machine_csrs.mepc == base + 2U);

    csrs.write_validated(rv64::csr_address::medeleg, ~std::uint64_t{0});
    CHECK(
        state.machine_csrs.medeleg ==
        rv64::supported_exception_delegation);
    csrs.write_validated(rv64::csr_address::mideleg, ~std::uint64_t{0});
    CHECK(
        state.machine_csrs.mideleg ==
        rv64::supported_interrupt_delegation);

    csrs.write_validated(rv64::csr_address::satp, 8ULL << 60U);
    CHECK(state.supervisor_csrs.satp == (8ULL << 60U));
    csrs.write_validated(rv64::csr_address::satp, 9ULL << 60U);
    CHECK(state.supervisor_csrs.satp == (8ULL << 60U));

    CHECK(
        csrs.validate_write(
            rv64::csr_address::cycle,
            rv64::PrivilegeMode::Machine) ==
        rv64::CsrAccessStatus::ReadOnly);
    CHECK(
        csrs.read(
            rv64::csr_address::mstatus,
            rv64::PrivilegeMode::Supervisor).status ==
        rv64::CsrAccessStatus::PrivilegeViolation);
}

void test_counter_permissions_and_supervisor_aliases()
{
    TestBus bus;
    rv64::CpuSnapshot state;
    state.machine_csrs.mstatus = rv64::sanitize_mstatus(0);
    state.machine_csrs.mcounteren = 0;
    state.supervisor_csrs.scounteren = 0;
    rv64::CsrFile csrs(state, bus);
    CHECK(
        csrs.read(
            rv64::csr_address::cycle,
            rv64::PrivilegeMode::Supervisor).status ==
        rv64::CsrAccessStatus::PrivilegeViolation);
    state.machine_csrs.mcounteren = 0x7U;
    CHECK(
        csrs.read(
            rv64::csr_address::cycle,
            rv64::PrivilegeMode::Supervisor).ready());
    CHECK(
        !csrs.read(
            rv64::csr_address::cycle,
            rv64::PrivilegeMode::User).ready());
    state.supervisor_csrs.scounteren = 0x7U;
    CHECK(
        csrs.read(
            rv64::csr_address::cycle,
            rv64::PrivilegeMode::User).ready());

    state.machine_csrs.mideleg =
        rv64::interrupt_bits::supervisor_software;
    csrs.write_validated(
        rv64::csr_address::sie,
        rv64::interrupt_bits::supervisor_software);
    CHECK(
        csrs.read(
            rv64::csr_address::sie,
            rv64::PrivilegeMode::Supervisor).value ==
        rv64::interrupt_bits::supervisor_software);
    csrs.write_validated(
        rv64::csr_address::sip,
        rv64::interrupt_bits::supervisor_software);
    CHECK(
        (csrs.read(
             rv64::csr_address::sip,
             rv64::PrivilegeMode::Supervisor).value &
         rv64::interrupt_bits::supervisor_software) != 0U);
}

void test_trap_delegation_and_returns()
{
    rv64::CpuSnapshot state;
    state.pc = base;
    state.privilege = rv64::PrivilegeMode::User;
    state.machine_csrs.mstatus = rv64::sanitize_mstatus(
        rv64::mstatus_bits::sie);
    state.machine_csrs.medeleg =
        1ULL << static_cast<std::uint64_t>(
            rv64::ExceptionCause::EnvironmentCallFromUser);
    state.supervisor_csrs.stvec = base + 0x100U;
    CHECK(
        rv64::take_trap(
            state,
            {
                .cause =
                    rv64::ExceptionCause::EnvironmentCallFromUser,
                .exception_pc = base + 4U,
                .trap_value = 0,
            }) == rv64::TrapTarget::Supervisor);
    CHECK(state.privilege == rv64::PrivilegeMode::Supervisor);
    CHECK(state.pc == base + 0x100U);
    CHECK(state.supervisor_csrs.sepc == base + 4U);
    CHECK(
        state.supervisor_csrs.scause ==
        static_cast<std::uint64_t>(
            rv64::ExceptionCause::EnvironmentCallFromUser));
    CHECK(
        (state.machine_csrs.mstatus & rv64::mstatus_bits::spie) != 0U);

    state.supervisor_csrs.sepc = base + 8U;
    CHECK(rv64::execute_sret(state));
    CHECK(state.privilege == rv64::PrivilegeMode::User);
    CHECK(state.pc == base + 8U);

    state.privilege = rv64::PrivilegeMode::Machine;
    state.machine_csrs.mepc = base + 12U;
    state.machine_csrs.mstatus = rv64::sanitize_mstatus(
        (static_cast<std::uint64_t>(rv64::PrivilegeMode::Supervisor)
         << rv64::mstatus_bits::mpp_shift) |
        rv64::mstatus_bits::mpie |
        rv64::mstatus_bits::mprv);
    CHECK(rv64::execute_mret(state));
    CHECK(state.privilege == rv64::PrivilegeMode::Supervisor);
    CHECK(state.pc == base + 12U);
    CHECK(
        (state.machine_csrs.mstatus & rv64::mstatus_bits::mie) != 0U);
    CHECK(
        (state.machine_csrs.mstatus & rv64::mstatus_bits::mprv) == 0U);
}

void test_interrupt_priority_and_vectored_entry()
{
    rv64::CpuSnapshot state;
    state.pc = base;
    state.privilege = rv64::PrivilegeMode::Machine;
    state.machine_csrs.mstatus = rv64::sanitize_mstatus(
        rv64::mstatus_bits::mie);
    state.machine_csrs.mie =
        rv64::interrupt_bits::machine_timer |
        rv64::interrupt_bits::machine_external;
    rv64::sample_interrupt_lines(
        state,
        {
            .machine_timer = true,
            .machine_external = true,
        });
    const auto selected = rv64::select_pending_interrupt(state);
    CHECK(selected.pending);
    CHECK(selected.cause == rv64::InterruptCause::MachineExternal);

    state.machine_csrs.mtvec = (base + 0x100U) | 1U;
    rv64::take_interrupt_trap(
        state,
        {
            .cause = selected.cause,
            .interrupted_pc = base,
        },
        selected.target);
    CHECK(state.pc == base + 0x100U + 4U * 11U);
    CHECK(state.machine_csrs.mepc == base);
    CHECK(
        state.machine_csrs.mcause ==
        ((1ULL << 63U) | 11U));
}

void test_core_csr_mret_and_wfi()
{
    TestBus bus;
    constexpr std::uint32_t program[]{
        encode_i(1U, 0U, 0U, 1U),
        encode_csr(rv64::csr_address::mscratch, 1U, 1U, 2U),
        encode_csr(rv64::csr_address::mscratch, 0U, 2U, 3U),
        0x00000217U, // AUIPC x4,0
        encode_i(28U, 4U, 0U, 4U),
        encode_csr(rv64::csr_address::mepc, 4U, 1U, 0U),
        encode_i(1U, 0U, 0U, 5U),
        encode_i(11U, 5U, 1U, 5U),
        encode_csr(rv64::csr_address::mstatus, 5U, 1U, 0U),
        0x30200073U,
        encode_i(7U, 0U, 0U, 6U),
    };
    bus.load_program(program);
    rv64::Core core(bus);
    core.reset({.reset_pc = base});
    for (int index = 0; index < 9; ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    const auto mret = core.step();
    CHECK(mret.status == rv64::StepStatus::Retired);
    CHECK(mret.privilege == rv64::PrivilegeMode::Machine);
    CHECK(core.snapshot().privilege == rv64::PrivilegeMode::Supervisor);
    CHECK(core.snapshot().pc == base + 40U);

    constexpr std::uint32_t wfi_program[]{
        encode_i(128U, 0U, 0U, 1U),
        encode_csr(rv64::csr_address::mie, 1U, 1U, 0U),
        encode_csr(rv64::csr_address::mstatus, 8U, 6U, 0U),
        0x10500073U,
    };
    bus.load_program(wfi_program);
    core.reset({.reset_pc = base});
    for (int index = 0; index < 4; ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    CHECK(core.snapshot().waiting_for_interrupt);
    CHECK(core.step().status == rv64::StepStatus::WaitingForInterrupt);
    const auto interrupt = core.step({.machine_timer = true});
    CHECK(interrupt.status == rv64::StepStatus::TrapTaken);
    CHECK(
        core.snapshot().machine_csrs.mcause ==
        ((1ULL << 63U) | 7U));
}

} // namespace

int main()
{
    test_privileged_decode();
    test_csr_width_aliases_and_warl();
    test_counter_permissions_and_supervisor_aliases();
    test_trap_delegation_and_returns();
    test_interrupt_priority_and_vectored_entry();
    test_core_csr_mret_and_wfi();
    if (failures == 0) {
        std::cout << "All independent RV64 privileged tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
