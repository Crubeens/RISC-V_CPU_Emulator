#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

#include "rv32/core/core.hpp"
#include "rv32/core/csr.hpp"
#include "rv32/core/execute.hpp"
#include "rv32/core/trap.hpp"
#include "rv/devices/ram.hpp"
#include "rv/platform/system_bus.hpp"

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
constexpr std::uint32_t pc = 0x80000000U;
constexpr rv32::CsrAddress test_csr = 0x800U;

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

[[nodiscard]] constexpr std::uint32_t encode_s(
    std::uint32_t immediate,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3) noexcept
{
    return (((immediate >> 5U) & 0x7FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((immediate & 0x1FU) << 7U) |
           0x23U;
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

[[nodiscard]] constexpr std::uint32_t encode_csr(
    rv32::CsrAddress address,
    std::uint32_t source_field,
    std::uint32_t funct3,
    std::uint32_t rd) noexcept
{
    return encode_i(address, source_field, funct3, rd, 0x73U);
}

[[nodiscard]] bool same_snapshot(
    const rv32::CpuSnapshot& left,
    const rv32::CpuSnapshot& right)
{
    return left == right;
}

class TestCsrAccess final : public rv32::CsrAccess {
  public:
    explicit TestCsrAccess(
        rv32::CsrAddress implemented_address,
        std::uint32_t initial_value) noexcept
        : value(initial_value),
          implemented_address_(implemented_address)
    {
    }

    rv32::CsrReadResult read(
        rv32::CsrAddress address,
        rv32::PrivilegeMode privilege) noexcept override
    {
        if (address != implemented_address_) {
            return {
                .status = rv32::CsrAccessStatus::NotFound,
                .value = 0,
            };
        }
        if (!rv32::csr_privilege_allows(address, privilege)) {
            return {
                .status =
                    rv32::CsrAccessStatus::PrivilegeViolation,
                .value = 0,
            };
        }

        ++read_count;
        return {
            .status = rv32::CsrAccessStatus::Ready,
            .value = value,
        };
    }

    rv32::CsrAccessStatus validate_write(
        rv32::CsrAddress address,
        rv32::PrivilegeMode privilege) noexcept override
    {
        ++validate_count;
        if (address != implemented_address_) {
            return rv32::CsrAccessStatus::NotFound;
        }
        if (!rv32::csr_privilege_allows(address, privilege)) {
            return rv32::CsrAccessStatus::PrivilegeViolation;
        }
        if (rv32::csr_is_read_only(address)) {
            return rv32::CsrAccessStatus::ReadOnly;
        }
        return rv32::CsrAccessStatus::Ready;
    }

    void write_validated(
        rv32::CsrAddress address,
        std::uint32_t new_value) noexcept override
    {
        if (address == implemented_address_) {
            ++write_count;
            value = new_value;
        }
    }

    std::uint32_t value{};
    std::uint32_t read_count{};
    std::uint32_t validate_count{};
    std::uint32_t write_count{};

  private:
    rv32::CsrAddress implemented_address_{};
};

class FixedTimeSource final : public rv::platform::TimeSource {
  public:
    explicit FixedTimeSource(std::uint64_t value) noexcept
        : value_(value)
    {
    }

    [[nodiscard]] std::uint64_t time_value() const noexcept override
    {
        return value_;
    }

  private:
    std::uint64_t value_{};
};

struct RmwCase {
    std::string_view name;
    std::uint32_t funct3{};
    rv32::InstructionKind kind{rv32::InstructionKind::Illegal};
    std::uint32_t initial{};
    std::uint32_t source_field{};
    std::uint32_t rs1_value{};
    std::uint32_t expected{};
};

constexpr std::array rmw_cases{
    RmwCase{
        "CSRRW",
        0x1U,
        rv32::InstructionKind::Csrrw,
        0x0F0F00F0U,
        1U,
        0x12345678U,
        0x12345678U,
    },
    RmwCase{
        "CSRRS",
        0x2U,
        rv32::InstructionKind::Csrrs,
        0x0F0F00F0U,
        1U,
        0x00F0000FU,
        0x0FFF00FFU,
    },
    RmwCase{
        "CSRRC",
        0x3U,
        rv32::InstructionKind::Csrrc,
        0xFFFF00FFU,
        1U,
        0x00FF000FU,
        0xFF0000F0U,
    },
    RmwCase{
        "CSRRWI",
        0x5U,
        rv32::InstructionKind::Csrrwi,
        0x0F0F00F0U,
        0x1AU,
        0U,
        0x1AU,
    },
    RmwCase{
        "CSRRSI",
        0x6U,
        rv32::InstructionKind::Csrrsi,
        0x10U,
        0x5U,
        0U,
        0x15U,
    },
    RmwCase{
        "CSRRCI",
        0x7U,
        rv32::InstructionKind::Csrrci,
        0x1FU,
        0x5U,
        0U,
        0x1AU,
    },
};

void test_all_csr_read_modify_write_forms()
{
    constexpr std::uint32_t rd = 7U;

    for (const auto& test : rmw_cases) {
        TestCsrAccess access(test_csr, test.initial);
        const std::uint32_t raw = encode_csr(
            test_csr,
            test.source_field,
            test.funct3,
            rd);
        const auto decoded = rv32::decode_instruction(raw);
        const auto result = rv32::execute_csr(
            access,
            decoded,
            rv32::PrivilegeMode::Machine,
            pc,
            test.rs1_value);

        rv32::CpuSnapshot state{};
        state.pc = pc;
        state.privilege = rv32::PrivilegeMode::Machine;
        const bool value_deferred = access.value == test.initial;
        const bool committed =
            rv32::commit_pending(state, result.pending, &access);

        const bool passed =
            decoded.kind == test.kind &&
            decoded.csr == test_csr &&
            result.ready() &&
            result.access_status == rv32::CsrAccessStatus::Ready &&
            result.trap_value == 0U &&
            result.pending.register_write.enabled &&
            result.pending.register_write.index == rd &&
            result.pending.register_write.value == test.initial &&
            result.pending.csr_write.enabled &&
            result.pending.csr_write.address == test_csr &&
            result.pending.csr_write.value == test.expected &&
            value_deferred &&
            committed &&
            access.value == test.expected &&
            access.read_count == 1U &&
            access.validate_count == 2U &&
            access.write_count == 1U &&
            state.registers[rd] == test.initial &&
            state.pc == pc + 4U &&
            state.instructions_retired == 1U;

        if (!passed) {
            std::cerr
                << "FAIL CSR case \"" << test.name
                << "\": expected old=0x" << std::hex
                << std::setw(8) << std::setfill('0')
                << test.initial
                << ", actual old=0x" << std::setw(8)
                << result.pending.register_write.value
                << ", expected new=0x" << std::setw(8)
                << test.expected
                << ", actual new=0x" << std::setw(8)
                << access.value
                << std::setfill(' ') << std::dec
                << ", reads=" << access.read_count
                << ", validates=" << access.validate_count
                << ", writes=" << access.write_count
                << ", ready=" << result.ready()
                << ", committed=" << committed
                << '\n';
            ++failures;
        }
    }
}

void test_csr_read_and_write_suppression_rules()
{
    {
        TestCsrAccess access(test_csr, 0x11111111U);
        const auto decoded = rv32::decode_instruction(
            encode_csr(test_csr, 1U, 0x1U, 0U));
        const auto result = rv32::execute_csr(
            access,
            decoded,
            rv32::PrivilegeMode::Machine,
            pc,
            0x22222222U);
        rv32::CpuSnapshot state{};
        state.pc = pc;
        state.privilege = rv32::PrivilegeMode::Machine;

        CHECK(result.ready());
        CHECK(access.read_count == 0U);
        CHECK(access.validate_count == 1U);
        CHECK(!result.pending.register_write.enabled);
        CHECK(result.pending.csr_write.enabled);
        CHECK(rv32::commit_pending(state, result.pending, &access));
        CHECK(access.value == 0x22222222U);
        CHECK(access.read_count == 0U);
        CHECK(access.write_count == 1U);
    }

    {
        TestCsrAccess access(test_csr, 0xFFFFFFFFU);
        const auto decoded = rv32::decode_instruction(
            encode_csr(test_csr, 0U, 0x5U, 0U));
        const auto result = rv32::execute_csr(
            access,
            decoded,
            rv32::PrivilegeMode::Machine,
            pc,
            0U);
        rv32::CpuSnapshot state{};
        state.pc = pc;
        state.privilege = rv32::PrivilegeMode::Machine;

        CHECK(result.ready());
        CHECK(access.read_count == 0U);
        CHECK(rv32::commit_pending(state, result.pending, &access));
        CHECK(access.value == 0U);
        CHECK(access.write_count == 1U);
    }

    struct ReadOnlyCase {
        std::uint32_t funct3{};
        std::uint32_t source_field{};
    };
    constexpr std::array read_only_cases{
        ReadOnlyCase{0x2U, 0U},
        ReadOnlyCase{0x3U, 0U},
        ReadOnlyCase{0x6U, 0U},
        ReadOnlyCase{0x7U, 0U},
    };

    for (const auto& test : read_only_cases) {
        TestCsrAccess access(
            rv32::csr_address::cycle,
            0x89ABCDEFU);
        const auto decoded = rv32::decode_instruction(
            encode_csr(
                rv32::csr_address::cycle,
                test.source_field,
                test.funct3,
                7U));
        const auto result = rv32::execute_csr(
            access,
            decoded,
            rv32::PrivilegeMode::User,
            pc,
            0U);
        rv32::CpuSnapshot state{};
        state.pc = pc;
        state.privilege = rv32::PrivilegeMode::User;

        CHECK(result.ready());
        CHECK(result.pending.register_write.value == 0x89ABCDEFU);
        CHECK(!result.pending.csr_write.enabled);
        CHECK(access.read_count == 1U);
        CHECK(access.validate_count == 0U);
        CHECK(rv32::commit_pending(state, result.pending, &access));
        CHECK(access.write_count == 0U);
        CHECK(state.registers[7] == 0x89ABCDEFU);
    }

    {
        TestCsrAccess access(
            rv32::csr_address::cycle,
            0x12345678U);
        const auto decoded = rv32::decode_instruction(
            encode_csr(
                rv32::csr_address::cycle,
                1U,
                0x2U,
                7U));
        const auto result = rv32::execute_csr(
            access,
            decoded,
            rv32::PrivilegeMode::Machine,
            pc,
            0U);

        CHECK(
            result.status ==
            rv32::CsrExecutionStatus::IllegalInstruction);
        CHECK(result.access_status == rv32::CsrAccessStatus::ReadOnly);
        CHECK(!result.pending.ready());
        CHECK(access.validate_count == 1U);
        CHECK(access.read_count == 0U);
        CHECK(access.write_count == 0U);
    }
}

void test_csr_permissions_and_missing_addresses()
{
    CHECK(!rv32::csr_is_read_only(0x300U));
    CHECK(rv32::csr_is_read_only(rv32::csr_address::cycle));
    CHECK(rv32::csr_minimum_privilege(0x000U) == 0U);
    CHECK(rv32::csr_minimum_privilege(0x100U) == 1U);
    CHECK(rv32::csr_minimum_privilege(0x300U) == 3U);
    CHECK(
        !rv32::csr_privilege_allows(
            0x300U,
            rv32::PrivilegeMode::Supervisor));
    CHECK(
        rv32::csr_privilege_allows(
            0x300U,
            rv32::PrivilegeMode::Machine));

    {
        TestCsrAccess machine_csr(0x300U, 0xA5A5A5A5U);
        const auto decoded = rv32::decode_instruction(
            encode_csr(0x300U, 0U, 0x2U, 1U));
        const auto denied = rv32::execute_csr(
            machine_csr,
            decoded,
            rv32::PrivilegeMode::Supervisor,
            pc,
            0U);
        CHECK(
            denied.status ==
            rv32::CsrExecutionStatus::IllegalInstruction);
        CHECK(
            denied.access_status ==
            rv32::CsrAccessStatus::PrivilegeViolation);
        CHECK(machine_csr.read_count == 0U);

        const auto allowed = rv32::execute_csr(
            machine_csr,
            decoded,
            rv32::PrivilegeMode::Machine,
            pc,
            0U);
        CHECK(allowed.ready());
        CHECK(allowed.pending.register_write.value == 0xA5A5A5A5U);
    }

    {
        TestCsrAccess supervisor_csr(0x100U, 0x55AA55AAU);
        const auto decoded = rv32::decode_instruction(
            encode_csr(0x100U, 0U, 0x2U, 1U));
        const auto denied = rv32::execute_csr(
            supervisor_csr,
            decoded,
            rv32::PrivilegeMode::User,
            pc,
            0U);
        CHECK(
            denied.access_status ==
            rv32::CsrAccessStatus::PrivilegeViolation);

        const auto allowed = rv32::execute_csr(
            supervisor_csr,
            decoded,
            rv32::PrivilegeMode::Supervisor,
            pc,
            0U);
        CHECK(allowed.ready());
    }

    {
        TestCsrAccess access(test_csr, 0U);
        const auto decoded = rv32::decode_instruction(
            encode_csr(0x801U, 0U, 0x2U, 1U));
        const auto result = rv32::execute_csr(
            access,
            decoded,
            rv32::PrivilegeMode::Machine,
            pc,
            0U);
        CHECK(
            result.status ==
            rv32::CsrExecutionStatus::IllegalInstruction);
        CHECK(result.access_status == rv32::CsrAccessStatus::NotFound);
        CHECK(result.trap_value == decoded.raw);
    }
}

void test_csr_write_is_not_applied_by_a_stale_commit()
{
    TestCsrAccess access(test_csr, 0x11111111U);
    const auto decoded = rv32::decode_instruction(
        encode_csr(test_csr, 1U, 0x1U, 7U));
    const auto result = rv32::execute_csr(
        access,
        decoded,
        rv32::PrivilegeMode::Machine,
        pc,
        0x22222222U);

    rv32::CpuSnapshot state{};
    state.pc = pc + 4U;
    state.privilege = rv32::PrivilegeMode::Machine;
    const auto before = state;

    CHECK(result.ready());
    CHECK(!rv32::commit_pending(state, result.pending, &access));
    CHECK(same_snapshot(state, before));
    CHECK(access.value == 0x11111111U);
    CHECK(access.write_count == 0U);
}

void test_csr_commit_validation_is_atomic()
{
    rv32::CpuSnapshot state{};
    state.pc = pc;
    state.privilege = rv32::PrivilegeMode::Machine;
    state.registers[7] = 0x11111111U;
    const auto before = state;

    const rv32::PendingCommit pending{
        .status = rv32::ExecuteStatus::Ready,
        .pc = pc,
        .instruction = encode_csr(test_csr, 1U, 0x1U, 7U),
        .next_pc = pc + 4U,
        .register_write = {
            .enabled = true,
            .index = 7U,
            .value = 0x22222222U,
        },
        .csr_write = {
            .enabled = true,
            .address = test_csr,
            .value = 0x33333333U,
        },
    };

    CHECK(!rv32::commit_pending(state, pending));
    CHECK(same_snapshot(state, before));

    TestCsrAccess read_only(
        rv32::csr_address::cycle,
        0x44444444U);
    rv32::PendingCommit read_only_pending = pending;
    read_only_pending.csr_write.address =
        rv32::csr_address::cycle;
    CHECK(
        !rv32::commit_pending(
            state,
            read_only_pending,
            &read_only));
    CHECK(same_snapshot(state, before));
    CHECK(read_only.value == 0x44444444U);
    CHECK(read_only.write_count == 0U);
}

void test_zicntr_direct_values()
{
    rv::platform::SystemBus bus;
    FixedTimeSource time_source(0xFEDCBA9876543210ULL);
    bus.set_time_source(&time_source);

    rv32::CpuSnapshot state{};
    state.cycle = 0x0123456789ABCDEFULL;
    state.instructions_retired = 0x89ABCDEF01234567ULL;
    rv32::CsrFile csr_file(state, bus);

    CHECK(
        csr_file
            .read(
                rv32::csr_address::cycle,
                rv32::PrivilegeMode::User)
            .value == 0x89ABCDEFU);
    CHECK(
        csr_file
            .read(
                rv32::csr_address::cycleh,
                rv32::PrivilegeMode::User)
            .value == 0x01234567U);
    CHECK(
        csr_file
            .read(
                rv32::csr_address::time,
                rv32::PrivilegeMode::User)
            .value == 0x76543210U);
    CHECK(
        csr_file
            .read(
                rv32::csr_address::timeh,
                rv32::PrivilegeMode::User)
            .value == 0xFEDCBA98U);
    CHECK(
        csr_file
            .read(
                rv32::csr_address::instret,
                rv32::PrivilegeMode::User)
            .value == 0x01234567U);
    CHECK(
        csr_file
            .read(
                rv32::csr_address::instreth,
                rv32::PrivilegeMode::User)
            .value == 0x89ABCDEFU);
    CHECK(
        csr_file.validate_write(
            rv32::csr_address::cycle,
            rv32::PrivilegeMode::Machine) ==
        rv32::CsrAccessStatus::ReadOnly);
    CHECK(
        csr_file
            .read(0xC03U, rv32::PrivilegeMode::Machine)
            .status == rv32::CsrAccessStatus::NotFound);
}

void test_zicntr_flows_through_core_step()
{
    rv::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv::devices::Ram>(
            ram_base,
            0x1000U));
    FixedTimeSource time_source(0xFEDCBA9876543210ULL);
    bus.set_time_source(&time_source);

    constexpr std::array program{
        encode_csr(rv32::csr_address::cycle, 0U, 0x2U, 1U),
        encode_csr(rv32::csr_address::time, 0U, 0x2U, 2U),
        encode_csr(rv32::csr_address::instret, 0U, 0x2U, 3U),
        encode_csr(rv32::csr_address::cycleh, 0U, 0x2U, 4U),
        encode_csr(rv32::csr_address::timeh, 0U, 0x2U, 5U),
        encode_csr(rv32::csr_address::instreth, 0U, 0x2U, 6U),
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

    rv32::Core core(bus);
    core.reset({
        .reset_pc = pc,
        .hart_id = 0,
        .initial_privilege = rv32::PrivilegeMode::User,
    });
    core.advance_cycles(0x0123456789ABCDEFULL);

    for (const std::uint32_t instruction : program) {
        const auto result = core.step({});
        CHECK(result.status == rv32::StepStatus::Retired);
        CHECK(result.instruction == instruction);
        if (result.status != rv32::StepStatus::Retired) {
            return;
        }
    }

    const auto state = core.snapshot();
    CHECK(state.registers[1] == 0x89ABCDEFU);
    CHECK(state.registers[2] == 0x76543210U);
    CHECK(state.registers[3] == 2U);
    CHECK(state.registers[4] == 0x01234567U);
    CHECK(state.registers[5] == 0xFEDCBA98U);
    CHECK(state.registers[6] == 0U);
    CHECK(state.instructions_retired == program.size());
    CHECK(
        state.pc ==
        pc + static_cast<std::uint32_t>(program.size() * 4U));
}

void test_illegal_csr_access_is_precise_in_core_step()
{
    constexpr std::array illegal_instructions{
        encode_csr(rv32::csr_address::cycle, 0U, 0x1U, 0U),
        encode_csr(rv32::csr_address::cycle, 1U, 0x2U, 2U),
        encode_csr(0x123U, 0U, 0x2U, 2U),
    };

    for (const std::uint32_t instruction : illegal_instructions) {
        rv::platform::SystemBus bus;
        static_cast<void>(
            bus.emplace_device<rv::devices::Ram>(
                ram_base,
                0x1000U));
        CHECK(
            bus.write(
                ram_base,
                rv32::AccessWidth::Word,
                instruction,
                rv32::AccessKind::Store) ==
            rv32::BusFault::None);

        rv32::Core core(bus);
        const auto before = core.snapshot();
        const auto result = core.step({});

        CHECK(result.status == rv32::StepStatus::TrapTaken);
        CHECK(result.pc == pc);
        CHECK(result.instruction == instruction);
        CHECK(result.trap_value == instruction);
        CHECK(result.bus_fault == rv32::BusFault::None);
        const auto after = core.snapshot();
        CHECK(after.registers == before.registers);
        CHECK(after.pc == 0U);
        CHECK(after.machine_csrs.mepc == pc);
        CHECK(
            after.machine_csrs.mcause ==
            static_cast<std::uint32_t>(
                rv32::ExceptionCause::IllegalInstruction));
        CHECK(after.machine_csrs.mtval == instruction);
        CHECK(
            after.machine_csrs.mstatus ==
            (static_cast<std::uint32_t>(
                 rv32::PrivilegeMode::Machine)
             << rv32::mstatus_bits::mpp_shift));
        CHECK(
            after.instructions_retired ==
            before.instructions_retired);
    }
}

void test_fence_i_with_self_modifying_code()
{
    rv::platform::SystemBus bus;
    static_cast<void>(
        bus.emplace_device<rv::devices::Ram>(
            ram_base,
            0x1000U));

    constexpr std::uint32_t patched_instruction =
        encode_i(2U, 0U, 0U, 1U, 0x13U);
    constexpr std::array program{
        encode_u(patched_instruction, 2U),
        encode_i(
            patched_instruction & 0xFFFU,
            2U,
            0U,
            2U,
            0x13U),
        encode_u(pc, 3U),
        encode_i(0x20U, 3U, 0U, 3U, 0x13U),
        encode_s(0U, 2U, 3U, 0x2U),
        std::uint32_t{0x0000100FU},
        encode_j(8U, 0U),
        encode_i(1U, 0U, 0U, 1U, 0x13U),
        encode_i(0U, 0U, 0U, 1U, 0x13U),
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

    rv32::Core core(bus);
    constexpr std::array expected_pcs{
        pc,
        pc + 4U,
        pc + 8U,
        pc + 12U,
        pc + 16U,
        pc + 20U,
        pc + 24U,
        pc + 32U,
    };

    for (const std::uint32_t expected_pc : expected_pcs) {
        const auto result = core.step({});
        CHECK(result.status == rv32::StepStatus::Retired);
        CHECK(result.pc == expected_pc);
        if (result.status != rv32::StepStatus::Retired) {
            return;
        }
    }

    const auto fetched_patch = bus.read(
        ram_base + 32U,
        rv32::AccessWidth::Word,
        rv32::AccessKind::InstructionFetch);
    CHECK(fetched_patch.ok());
    CHECK(fetched_patch.value == patched_instruction);
    CHECK(core.snapshot().registers[1] == 2U);
    CHECK(core.snapshot().pc == pc + 36U);
    CHECK(core.snapshot().instructions_retired == expected_pcs.size());
}

} // namespace

int main()
{
    test_all_csr_read_modify_write_forms();
    test_csr_read_and_write_suppression_rules();
    test_csr_permissions_and_missing_addresses();
    test_csr_write_is_not_applied_by_a_stale_commit();
    test_csr_commit_validation_is_atomic();
    test_zicntr_direct_values();
    test_zicntr_flows_through_core_step();
    test_illegal_csr_access_is_precise_in_core_step();
    test_fence_i_with_self_modifying_code();

    if (failures == 0) {
        std::cout
            << "All Zicsr, Zicntr, and Zifencei tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
