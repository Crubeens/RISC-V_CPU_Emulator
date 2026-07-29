#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

#include "rv/common/bus.hpp"
#include "rv64/core/core.hpp"
#include "rv64/core/csr.hpp"
#include "rv64/core/decode.hpp"
#include "rv64/core/trap.hpp"

namespace {

constexpr std::uint64_t base = 0x80000000ULL;
constexpr std::size_t memory_size = 4096U;
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
        if (!contains(address, count)) {
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
        static_cast<void>(kind);
        const std::uint64_t count = rv::width_bytes(width);
        if ((address & (count - 1U)) != 0U) {
            return rv::BusFault::Misaligned;
        }
        if (!contains(address, count)) {
            return rv::BusFault::Unmapped;
        }
        put(address, value, static_cast<std::size_t>(count));
        return rv::BusFault::None;
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
        return 0;
    }

    void load_program(std::span<const std::uint32_t> program)
    {
        bytes_.fill(0);
        for (std::size_t index = 0; index < program.size(); ++index) {
            put(
                base + index * sizeof(std::uint32_t),
                program[index],
                sizeof(std::uint32_t));
        }
    }

    void put16(std::uint64_t address, std::uint16_t value)
    {
        put(address, value, sizeof(value));
    }

    void put32(std::uint64_t address, std::uint32_t value)
    {
        put(address, value, sizeof(value));
    }

    void put64(std::uint64_t address, std::uint64_t value)
    {
        put(address, value, sizeof(value));
    }

    [[nodiscard]] std::uint64_t get64(std::uint64_t address)
    {
        return read(
                   address,
                   rv::AccessWidth::DoubleWord,
                   rv::AccessKind::Load)
            .value;
    }

    [[nodiscard]] std::uint32_t get32(std::uint64_t address)
    {
        return static_cast<std::uint32_t>(
            read(
                address,
                rv::AccessWidth::Word,
                rv::AccessKind::Load)
                .value);
    }

  private:
    [[nodiscard]] bool contains(
        std::uint64_t address,
        std::uint64_t count) const noexcept
    {
        return address >= base &&
               count <= bytes_.size() &&
               address - base <= bytes_.size() - count;
    }

    void put(
        std::uint64_t address,
        std::uint64_t value,
        std::size_t count)
    {
        const std::size_t offset =
            static_cast<std::size_t>(address - base);
        for (std::size_t index = 0; index < count; ++index) {
            bytes_[offset + index] = static_cast<std::uint8_t>(
                value >> (index * 8U));
        }
    }

    std::array<std::uint8_t, memory_size> bytes_{};
};

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t immediate,
    std::uint32_t rd)
{
    return (immediate & 0xFFFFF000U) |
           ((rd & 0x1FU) << 7U) |
           0x37U;
}

[[nodiscard]] constexpr std::uint32_t encode_csr(
    std::uint32_t csr,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd)
{
    return ((csr & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x73U;
}

[[nodiscard]] constexpr std::uint32_t encode_fp_load(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t funct3,
    std::uint32_t rd)
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x07U;
}

[[nodiscard]] constexpr std::uint32_t encode_fp_store(
    std::uint32_t immediate,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t funct3)
{
    return (((immediate >> 5U) & 0x7FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((funct3 & 0x7U) << 12U) |
           ((immediate & 0x1FU) << 7U) |
           0x27U;
}

[[nodiscard]] constexpr std::uint32_t encode_fmv(
    std::uint32_t match,
    std::uint32_t rs1,
    std::uint32_t rd)
{
    return match |
           ((rs1 & 0x1FU) << 15U) |
           ((rd & 0x1FU) << 7U);
}

constexpr std::array<std::uint32_t, 2> enable_floating_point{
    encode_u(0x2000U, 1U),
    encode_csr(rv64::csr_address::mstatus, 1U, 1U, 0U),
};

void check_decode(
    std::uint32_t raw,
    rv64::InstructionKind kind,
    std::uint8_t rd,
    std::uint8_t rs1,
    std::uint8_t rs2,
    std::uint64_t immediate)
{
    const auto decoded = rv64::decode_instruction(raw);
    CHECK(decoded.kind == kind);
    CHECK(decoded.rd == rd);
    CHECK(decoded.rs1 == rs1);
    CHECK(decoded.rs2 == rs2);
    CHECK(decoded.immediate == immediate);
    CHECK(decoded.length == 4U);
}

void check_compressed_decode(
    std::uint16_t raw,
    rv64::InstructionKind kind,
    std::uint8_t rd,
    std::uint8_t rs1,
    std::uint8_t rs2,
    std::uint64_t immediate)
{
    const auto decoded = rv64::decode_compressed_instruction(raw);
    CHECK(decoded.kind == kind);
    CHECK(decoded.rd == rd);
    CHECK(decoded.rs1 == rs1);
    CHECK(decoded.rs2 == rs2);
    CHECK(decoded.immediate == immediate);
    CHECK(decoded.length == 2U);
}

void test_decode()
{
    using K = rv64::InstructionKind;
    check_decode(
        encode_fp_load(0x7FCU, 4U, 2U, 3U),
        K::Flw,
        3U,
        4U,
        28U,
        0x7FCU);
    check_decode(
        encode_fp_load(0xFF8U, 6U, 3U, 5U),
        K::Fld,
        5U,
        6U,
        24U,
        0xFFFFFFFFFFFFFFF8ULL);
    check_decode(
        encode_fp_store(0x7FCU, 3U, 4U, 2U),
        K::Fsw,
        28U,
        4U,
        3U,
        0x7FCU);
    check_decode(
        encode_fp_store(0xFF8U, 5U, 6U, 3U),
        K::Fsd,
        24U,
        6U,
        5U,
        0xFFFFFFFFFFFFFFF8ULL);
    check_decode(
        encode_fmv(0xE0000053U, 7U, 8U),
        K::FmvXW,
        8U,
        7U,
        0U,
        0U);
    check_decode(
        encode_fmv(0xF0000053U, 9U, 10U),
        K::FmvWX,
        10U,
        9U,
        0U,
        0U);
    check_decode(
        encode_fmv(0xE2000053U, 11U, 12U),
        K::FmvXD,
        12U,
        11U,
        0U,
        0U);
    check_decode(
        encode_fmv(0xF2000053U, 13U, 14U),
        K::FmvDX,
        14U,
        13U,
        0U,
        0U);

    check_compressed_decode(0x2000U, K::Fld, 8U, 8U, 0U, 0U);
    check_compressed_decode(0xA000U, K::Fsd, 0U, 8U, 8U, 0U);
    check_compressed_decode(0x2002U, K::Fld, 0U, 2U, 0U, 0U);
    check_compressed_decode(0xA002U, K::Fsd, 0U, 2U, 0U, 0U);
    check_compressed_decode(0xB920U, K::Fsd, 0U, 10U, 8U, 112U);
}

void test_floating_csrs_and_fs_state()
{
    TestBus bus;
    rv64::CpuSnapshot state;
    state.machine_csrs.mstatus = rv64::sanitize_mstatus(0);
    rv64::CsrFile csrs(state, bus);

    CHECK(!rv64::floating_point_enabled(state));
    CHECK(
        csrs.read(
                rv64::csr_address::fcsr,
                rv64::PrivilegeMode::Machine)
            .status == rv64::CsrAccessStatus::PrivilegeViolation);
    CHECK(
        csrs.validate_write(
                rv64::csr_address::fcsr,
                rv64::PrivilegeMode::Machine) ==
        rv64::CsrAccessStatus::PrivilegeViolation);

    csrs.write_validated(
        rv64::csr_address::mstatus,
        rv64::mstatus_bits::fs_initial);
    CHECK(rv64::floating_point_enabled(state));
    CHECK(
        (state.machine_csrs.mstatus & rv64::mstatus_bits::fs) ==
        rv64::mstatus_bits::fs_initial);
    CHECK(
        (state.machine_csrs.mstatus & rv64::mstatus_bits::sd) == 0U);

    CHECK(
        csrs.validate_write(
                rv64::csr_address::fcsr,
                rv64::PrivilegeMode::User) ==
        rv64::CsrAccessStatus::Ready);
    csrs.write_validated(rv64::csr_address::fcsr, 0x1FFU);
    CHECK(state.floating_point.fcsr == 0xFFU);
    CHECK(
        (state.machine_csrs.mstatus & rv64::mstatus_bits::fs) ==
        rv64::mstatus_bits::fs_dirty);
    CHECK(
        (state.machine_csrs.mstatus & rv64::mstatus_bits::sd) != 0U);
    CHECK(
        csrs.read(
                rv64::csr_address::fflags,
                rv64::PrivilegeMode::User)
            .value == 0x1FU);
    CHECK(
        csrs.read(
                rv64::csr_address::frm,
                rv64::PrivilegeMode::User)
            .value == 0x7U);

    csrs.write_validated(rv64::csr_address::fflags, 0x2AU);
    csrs.write_validated(rv64::csr_address::frm, 0x3U);
    CHECK(state.floating_point.fcsr == 0x6AU);
    CHECK(
        csrs.read(
                rv64::csr_address::fcsr,
                rv64::PrivilegeMode::Supervisor)
            .value == 0x6AU);
    const auto sstatus = csrs.read(
        rv64::csr_address::sstatus,
        rv64::PrivilegeMode::Supervisor);
    CHECK(sstatus.ready());
    CHECK((sstatus.value & rv64::mstatus_bits::fs) ==
          rv64::mstatus_bits::fs_dirty);
    CHECK((sstatus.value & rv64::mstatus_bits::sd) != 0U);

    csrs.write_validated(
        rv64::csr_address::mstatus,
        rv64::mstatus_bits::fs_clean);
    CHECK((state.machine_csrs.mstatus & rv64::mstatus_bits::sd) == 0U);
    CHECK(
        csrs.read(
                rv64::csr_address::fcsr,
                rv64::PrivilegeMode::User)
            .ready());
    csrs.write_validated(rv64::csr_address::sstatus, 0U);
    CHECK(!rv64::floating_point_enabled(state));
}

void test_loads_and_moves()
{
    constexpr std::uint64_t data = base + 0x200U;
    TestBus bus;
    const std::array program{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_fp_load(0U, 11U, 2U, 3U),
        encode_fp_load(8U, 11U, 3U, 4U),
    };
    bus.load_program(program);
    bus.put32(data, 0x80000001U);
    bus.put64(data + 8U, 0x0123456789ABCDEFULL);

    rv64::Core core(bus);
    core.reset({.reset_pc = base, .boot_argument = data});
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    const auto flw = core.step();
    CHECK(flw.status == rv64::StepStatus::Retired);
    CHECK(flw.floating_register_write.enabled);
    CHECK(flw.floating_register_write.index == 3U);
    CHECK(
        flw.floating_register_write.value ==
        0xFFFFFFFF80000001ULL);
    const auto fld = core.step();
    CHECK(fld.status == rv64::StepStatus::Retired);
    CHECK(
        core.snapshot().floating_point.registers[3] ==
        0xFFFFFFFF80000001ULL);
    CHECK(
        core.snapshot().floating_point.registers[4] ==
        0x0123456789ABCDEFULL);
    CHECK(
        (core.snapshot().machine_csrs.mstatus &
         rv64::mstatus_bits::fs) == rv64::mstatus_bits::fs_dirty);
    CHECK(
        (core.snapshot().machine_csrs.mstatus &
         rv64::mstatus_bits::sd) != 0U);

    constexpr std::uint64_t integer_value =
        0x80000001ABCDEF01ULL;
    const std::array move_program{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_fmv(0xF0000053U, 10U, 1U),
        encode_fmv(0xE0000053U, 1U, 2U),
        encode_fmv(0xF2000053U, 10U, 3U),
        encode_fmv(0xE2000053U, 3U, 4U),
    };
    bus.load_program(move_program);
    core.reset({.reset_pc = base, .hart_id = integer_value});
    for (std::size_t index = 0; index < move_program.size(); ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    CHECK(
        core.snapshot().floating_point.registers[1] ==
        0xFFFFFFFFABCDEF01ULL);
    CHECK(core.snapshot().registers[2] == 0xFFFFFFFFABCDEF01ULL);
    CHECK(
        core.snapshot().floating_point.registers[3] ==
        integer_value);
    CHECK(core.snapshot().registers[4] == integer_value);
}

void test_stores_and_debian_compressed_fsd()
{
    constexpr std::uint64_t data = base + 0x200U;
    constexpr std::uint64_t value = 0xFEDCBA9876543210ULL;
    TestBus bus;
    const std::array store_program{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_fmv(0xF2000053U, 10U, 5U),
        encode_fp_store(0U, 5U, 11U, 3U),
        encode_fp_store(8U, 5U, 11U, 2U),
    };
    bus.load_program(store_program);
    rv64::Core core(bus);
    core.reset({
        .reset_pc = base,
        .hart_id = value,
        .boot_argument = data,
    });
    for (std::size_t index = 0; index < store_program.size(); ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    CHECK(bus.get64(data) == value);
    CHECK(bus.get32(data + 8U) == static_cast<std::uint32_t>(value));

    const std::array compressed_program{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_fp_load(0U, 10U, 3U, 8U),
    };
    bus.load_program(compressed_program);
    bus.put16(base + 12U, 0xB920U);
    bus.put64(data, value);
    bus.put64(data + 112U, 0U);
    core.reset({.reset_pc = base, .hart_id = data});
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    const auto compressed_store = core.step();
    CHECK(compressed_store.status == rv64::StepStatus::Retired);
    CHECK(compressed_store.instruction == 0xB920U);
    CHECK(bus.get64(data + 112U) == value);
}

void test_disabled_and_faulting_operations_are_precise()
{
    constexpr std::uint64_t data = base + 0x200U;
    TestBus bus;
    bus.load_program(std::array{
        encode_fp_load(0U, 11U, 3U, 1U),
    });
    rv64::Core core(bus);
    core.reset({.reset_pc = base, .boot_argument = data});
    const auto disabled = core.step();
    CHECK(disabled.status == rv64::StepStatus::TrapTaken);
    CHECK(
        core.snapshot().machine_csrs.mcause ==
        static_cast<std::uint64_t>(
            rv64::ExceptionCause::IllegalInstruction));
    CHECK(core.snapshot().instructions_retired == 0U);
    CHECK(core.snapshot().floating_point.registers[1] == 0U);

    const std::array misaligned_load{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_fp_load(4U, 11U, 3U, 1U),
    };
    bus.load_program(misaligned_load);
    core.reset({.reset_pc = base, .boot_argument = data});
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    const auto load_fault = core.step();
    CHECK(load_fault.status == rv64::StepStatus::TrapTaken);
    CHECK(load_fault.bus_fault == rv::BusFault::Misaligned);
    CHECK(load_fault.trap_value == data + 4U);
    CHECK(core.snapshot().floating_point.registers[1] == 0U);
    CHECK(
        (core.snapshot().machine_csrs.mstatus &
         rv64::mstatus_bits::fs) == rv64::mstatus_bits::fs_initial);
    CHECK(
        (core.snapshot().machine_csrs.mstatus &
         rv64::mstatus_bits::sd) == 0U);

    const std::array access_load{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_fp_load(0U, 11U, 3U, 1U),
    };
    bus.load_program(access_load);
    core.reset({
        .reset_pc = base,
        .boot_argument = base + memory_size,
    });
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    const auto access_fault = core.step();
    CHECK(access_fault.status == rv64::StepStatus::TrapTaken);
    CHECK(access_fault.bus_fault == rv::BusFault::Unmapped);
    CHECK(core.snapshot().floating_point.registers[1] == 0U);

    const std::array misaligned_store{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_fp_store(4U, 0U, 11U, 3U),
    };
    bus.load_program(misaligned_store);
    core.reset({.reset_pc = base, .boot_argument = data});
    CHECK(core.step().status == rv64::StepStatus::Retired);
    CHECK(core.step().status == rv64::StepStatus::Retired);
    const auto store_fault = core.step();
    CHECK(store_fault.status == rv64::StepStatus::TrapTaken);
    CHECK(store_fault.bus_fault == rv::BusFault::Misaligned);
    CHECK(store_fault.trap_value == data + 4U);
    CHECK(
        (core.snapshot().machine_csrs.mstatus &
         rv64::mstatus_bits::fs) == rv64::mstatus_bits::fs_initial);
}

void test_reference_and_fast_modes_match()
{
    constexpr std::uint64_t data = base + 0x200U;
    constexpr std::uint64_t value = 0x8877665544332211ULL;
    const std::array program{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_fp_load(0U, 11U, 3U, 2U),
        encode_fmv(0xE2000053U, 2U, 3U),
        encode_fmv(0xF0000053U, 3U, 4U),
        encode_fp_store(8U, 4U, 11U, 2U),
    };

    TestBus reference_bus;
    TestBus fast_bus;
    reference_bus.load_program(program);
    fast_bus.load_program(program);
    reference_bus.put64(data, value);
    fast_bus.put64(data, value);

    rv64::Core reference(reference_bus);
    rv64::Core fast(fast_bus);
    reference.set_execution_mode(rv64::ExecutionMode::Reference);
    fast.set_execution_mode(rv64::ExecutionMode::Fast);
    reference.reset({.reset_pc = base, .boot_argument = data});
    fast.reset({.reset_pc = base, .boot_argument = data});

    for (std::size_t index = 0; index < program.size(); ++index) {
        const auto reference_result = reference.step();
        const auto fast_result = fast.step();
        CHECK(reference_result.status == fast_result.status);
        CHECK(
            reference_result.register_write.enabled ==
            fast_result.register_write.enabled);
        CHECK(
            reference_result.register_write.index ==
            fast_result.register_write.index);
        CHECK(
            reference_result.register_write.value ==
            fast_result.register_write.value);
        CHECK(
            reference_result.floating_register_write.enabled ==
            fast_result.floating_register_write.enabled);
        CHECK(
            reference_result.floating_register_write.index ==
            fast_result.floating_register_write.index);
        CHECK(
            reference_result.floating_register_write.value ==
            fast_result.floating_register_write.value);
        CHECK(reference.snapshot() == fast.snapshot());
    }
    CHECK(reference_bus.get32(data + 8U) == fast_bus.get32(data + 8U));
}

} // namespace

int main()
{
    test_decode();
    test_floating_csrs_and_fs_state();
    test_loads_and_moves();
    test_stores_and_debian_compressed_fsd();
    test_disabled_and_faulting_operations_are_precise();
    test_reference_and_fast_modes_match();

    if (failures == 0) {
        std::cout << "All RV64 floating-state M10.1 tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
