#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

#include "rv/common/bus.hpp"
#include "rv64/core/core.hpp"
#include "rv64/core/csr.hpp"
#include "rv64/core/decode.hpp"
#include "rv64/core/floating.hpp"

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
        const auto offset = static_cast<std::size_t>(address - base);
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

    void put32(std::uint64_t address, std::uint32_t value)
    {
        put(address, value, sizeof(value));
    }

  private:
    [[nodiscard]] bool contains(
        std::uint64_t address,
        std::uint64_t count) const noexcept
    {
        return address >= base && count <= bytes_.size() &&
               address - base <= bytes_.size() - count;
    }

    void put(
        std::uint64_t address,
        std::uint64_t value,
        std::size_t count)
    {
        const auto offset = static_cast<std::size_t>(address - base);
        for (std::size_t index = 0; index < count; ++index) {
            bytes_[offset + index] = static_cast<std::uint8_t>(
                value >> (index * 8U));
        }
    }

    std::array<std::uint8_t, memory_size> bytes_{};
};

[[nodiscard]] constexpr std::uint32_t encode_u(
    std::uint32_t immediate,
    std::uint32_t rd,
    std::uint32_t opcode)
{
    return (immediate & 0xFFFFF000U) |
           ((rd & 0x1FU) << 7U) |
           opcode;
}

[[nodiscard]] constexpr std::uint32_t encode_csr(
    std::uint32_t csr,
    std::uint32_t rs1,
    std::uint32_t rd)
{
    return ((csr & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           (1U << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x73U;
}

[[nodiscard]] constexpr std::uint32_t encode_flw(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t rd)
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           (2U << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x07U;
}

[[nodiscard]] constexpr std::uint32_t encode_addi(
    std::uint32_t immediate,
    std::uint32_t rs1,
    std::uint32_t rd)
{
    return ((immediate & 0xFFFU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((rd & 0x1FU) << 7U) |
           0x13U;
}

[[nodiscard]] constexpr std::uint32_t encode_op_fp(
    std::uint32_t funct7,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t rounding_mode,
    std::uint32_t rd)
{
    return ((funct7 & 0x7FU) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((rounding_mode & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           0x53U;
}

[[nodiscard]] constexpr std::uint32_t encode_fused(
    std::uint32_t opcode,
    std::uint32_t format,
    std::uint32_t rs3,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t rounding_mode,
    std::uint32_t rd)
{
    return ((rs3 & 0x1FU) << 27U) |
           ((format & 0x3U) << 25U) |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((rounding_mode & 0x7U) << 12U) |
           ((rd & 0x1FU) << 7U) |
           opcode;
}

constexpr std::array<std::uint32_t, 2> enable_floating_point{
    encode_u(0x2000U, 1U, 0x37U),
    encode_csr(rv64::csr_address::mstatus, 1U, 0U),
};

void test_decode()
{
    using K = rv64::InstructionKind;
    struct Case {
        std::uint32_t raw;
        K kind;
    };
    constexpr std::array cases{
        Case{encode_op_fp(0x00U, 2U, 1U, 0U, 3U), K::FaddS},
        Case{encode_op_fp(0x04U, 2U, 1U, 1U, 3U), K::FsubS},
        Case{encode_op_fp(0x08U, 2U, 1U, 2U, 3U), K::FmulS},
        Case{encode_op_fp(0x0CU, 2U, 1U, 3U, 3U), K::FdivS},
        Case{encode_op_fp(0x2CU, 0U, 1U, 4U, 3U), K::FsqrtS},
        Case{encode_op_fp(0x01U, 2U, 1U, 7U, 3U), K::FaddD},
        Case{encode_op_fp(0x05U, 2U, 1U, 0U, 3U), K::FsubD},
        Case{encode_op_fp(0x09U, 2U, 1U, 0U, 3U), K::FmulD},
        Case{encode_op_fp(0x0DU, 2U, 1U, 0U, 3U), K::FdivD},
        Case{encode_op_fp(0x2DU, 0U, 1U, 0U, 3U), K::FsqrtD},
        Case{encode_fused(0x43U, 0U, 4U, 2U, 1U, 0U, 3U), K::FmaddS},
        Case{encode_fused(0x47U, 0U, 4U, 2U, 1U, 0U, 3U), K::FmsubS},
        Case{encode_fused(0x4BU, 0U, 4U, 2U, 1U, 0U, 3U), K::FnmsubS},
        Case{encode_fused(0x4FU, 0U, 4U, 2U, 1U, 0U, 3U), K::FnmaddS},
        Case{encode_fused(0x43U, 1U, 4U, 2U, 1U, 0U, 3U), K::FmaddD},
        Case{encode_fused(0x47U, 1U, 4U, 2U, 1U, 0U, 3U), K::FmsubD},
        Case{encode_fused(0x4BU, 1U, 4U, 2U, 1U, 0U, 3U), K::FnmsubD},
        Case{encode_fused(0x4FU, 1U, 4U, 2U, 1U, 0U, 3U), K::FnmaddD},
    };

    for (const auto& test : cases) {
        const auto decoded = rv64::decode_instruction(test.raw);
        CHECK(decoded.kind == test.kind);
        CHECK(decoded.rd == 3U);
        CHECK(decoded.rs1 == 1U);
        CHECK(decoded.rs2 == 2U ||
              test.kind == K::FsqrtS ||
              test.kind == K::FsqrtD);
        if ((test.raw & 0x7FU) != 0x53U) {
            CHECK(decoded.rs3 == 4U);
        }
    }

    CHECK(
        !rv64::decode_instruction(
             encode_op_fp(0x00U, 2U, 1U, 5U, 3U))
             .valid());
    CHECK(
        !rv64::decode_instruction(
             encode_op_fp(0x2CU, 1U, 1U, 0U, 3U))
             .valid());
    CHECK(
        !rv64::decode_instruction(
             encode_fused(0x43U, 2U, 4U, 2U, 1U, 0U, 3U))
             .valid());
}

void test_rounding_and_exceptions()
{
    using F = rv64::FloatingFormat;
    using O = rv64::FloatingArithmeticOperation;

    const auto nearest = rv64::floating_arithmetic(
        F::Single,
        O::Add,
        0U,
        rv64::box_single(0x3F800000U),
        rv64::box_single(0x33800000U));
    CHECK(nearest.value == rv64::box_single(0x3F800000U));
    CHECK(nearest.exception_flags == 0x01U);

    const auto toward_zero = rv64::floating_arithmetic(
        F::Single,
        O::Add,
        1U,
        rv64::box_single(0x3F800000U),
        rv64::box_single(0x33800000U));
    CHECK(toward_zero.value == rv64::box_single(0x3F800000U));
    CHECK(toward_zero.exception_flags == 0x01U);

    const auto minimum = rv64::floating_arithmetic(
        F::Single,
        O::Add,
        2U,
        rv64::box_single(0xBF800000U),
        rv64::box_single(0xB3800000U));
    CHECK(minimum.value == rv64::box_single(0xBF800001U));
    CHECK(minimum.exception_flags == 0x01U);

    const auto maximum = rv64::floating_arithmetic(
        F::Single,
        O::Add,
        3U,
        rv64::box_single(0x3F800000U),
        rv64::box_single(0x33800000U));
    CHECK(maximum.value == rv64::box_single(0x3F800001U));
    CHECK(maximum.exception_flags == 0x01U);

    const auto ties_away = rv64::floating_arithmetic(
        F::Single,
        O::Add,
        4U,
        rv64::box_single(0x3F800000U),
        rv64::box_single(0x33800000U));
    CHECK(ties_away.value == rv64::box_single(0x3F800001U));
    CHECK(ties_away.exception_flags == 0x01U);

    const auto divide_by_zero = rv64::floating_arithmetic(
        F::Single,
        O::Divide,
        0U,
        rv64::box_single(0x3F800000U),
        rv64::box_single(0x00000000U));
    CHECK(divide_by_zero.value == rv64::box_single(0x7F800000U));
    CHECK(divide_by_zero.exception_flags == 0x08U);

    const auto invalid = rv64::floating_arithmetic(
        F::Single,
        O::SquareRoot,
        0U,
        rv64::box_single(0xBF800000U));
    CHECK(invalid.value == rv64::box_single(rv64::canonical_nan32));
    CHECK(invalid.exception_flags == 0x10U);

    const auto overflow = rv64::floating_arithmetic(
        F::Single,
        O::Multiply,
        0U,
        rv64::box_single(0x7F7FFFFFU),
        rv64::box_single(0x40000000U));
    CHECK(overflow.value == rv64::box_single(0x7F800000U));
    CHECK(overflow.exception_flags == 0x05U);

    const auto underflow = rv64::floating_arithmetic(
        F::Single,
        O::Divide,
        0U,
        rv64::box_single(0x00800000U),
        rv64::box_single(0x40400000U));
    CHECK(underflow.value == rv64::box_single(0x002AAAABU));
    CHECK(underflow.exception_flags == 0x03U);

    const auto signaling_nan = rv64::floating_arithmetic(
        F::Double,
        O::Add,
        0U,
        0x7FF0000000000001ULL,
        0x3FF0000000000000ULL);
    CHECK(signaling_nan.value == rv64::canonical_nan64);
    CHECK(signaling_nan.exception_flags == 0x10U);

    const auto invalid_box = rv64::floating_arithmetic(
        F::Single,
        O::Add,
        0U,
        0x000000003F800000ULL,
        rv64::box_single(0x3F800000U));
    CHECK(invalid_box.value == rv64::box_single(rv64::canonical_nan32));
    CHECK(invalid_box.exception_flags == 0U);
}

void test_basic_and_fused_arithmetic()
{
    using F = rv64::FloatingFormat;
    using O = rv64::FloatingArithmeticOperation;

    const auto add_single = rv64::floating_arithmetic(
        F::Single,
        O::Add,
        0U,
        rv64::box_single(0x3FC00000U),
        rv64::box_single(0x40100000U));
    CHECK(add_single.value == rv64::box_single(0x40700000U));
    CHECK(add_single.exception_flags == 0U);

    const auto add_double = rv64::floating_arithmetic(
        F::Double,
        O::Add,
        0U,
        0x3FF8000000000000ULL,
        0x4002000000000000ULL);
    CHECK(add_double.value == 0x400E000000000000ULL);
    CHECK(add_double.exception_flags == 0U);

    const auto fused = rv64::floating_arithmetic(
        F::Single,
        O::MultiplyAdd,
        0U,
        rv64::box_single(0x3FC00000U),
        rv64::box_single(0x40000000U),
        rv64::box_single(0x3F000000U));
    CHECK(fused.value == rv64::box_single(0x40600000U));
    CHECK(fused.exception_flags == 0U);

    const auto multiply_subtract = rv64::floating_arithmetic(
        F::Double,
        O::MultiplySubtract,
        0U,
        0x4000000000000000ULL,
        0x4008000000000000ULL,
        0x3FF0000000000000ULL);
    CHECK(multiply_subtract.value == 0x4014000000000000ULL);

    const auto negated_multiply_add = rv64::floating_arithmetic(
        F::Double,
        O::NegatedMultiplyAdd,
        0U,
        0x4000000000000000ULL,
        0x4008000000000000ULL,
        0x3FF0000000000000ULL);
    CHECK(negated_multiply_add.value == 0xC014000000000000ULL);

    const auto negated_multiply_subtract = rv64::floating_arithmetic(
        F::Double,
        O::NegatedMultiplySubtract,
        0U,
        0x4000000000000000ULL,
        0x4008000000000000ULL,
        0x3FF0000000000000ULL);
    CHECK(negated_multiply_subtract.value == 0xC01C000000000000ULL);
}

void test_core_execution_and_flag_accrual()
{
    const std::array program{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_u(0U, 1U, 0x17U),
        encode_flw(0x100U, 1U, 1U),
        encode_flw(0x104U, 1U, 2U),
        encode_op_fp(0x0CU, 2U, 1U, 0U, 3U),
        encode_flw(0x108U, 1U, 4U),
        encode_op_fp(0x2CU, 0U, 4U, 0U, 5U),
    };
    TestBus bus;
    bus.load_program(program);
    bus.put32(base + 0x108U, 0x3F800000U);
    bus.put32(base + 0x10CU, 0x00000000U);
    bus.put32(base + 0x110U, 0xBF800000U);

    rv64::Core core(bus);
    core.reset();
    rv64::StepResult result;
    for (std::size_t index = 0; index < program.size(); ++index) {
        result = core.step();
        CHECK(result.status == rv64::StepStatus::Retired);
    }

    CHECK(
        core.snapshot().floating_point.registers[3] ==
        rv64::box_single(0x7F800000U));
    CHECK(
        core.snapshot().floating_point.registers[5] ==
        rv64::box_single(rv64::canonical_nan32));
    CHECK((core.snapshot().floating_point.fcsr & 0x1FU) == 0x18U);
    CHECK(result.floating_register_write.enabled);
    CHECK(result.floating_register_write.index == 5U);
}

void test_dynamic_rounding_validation()
{
    std::uint8_t resolved = 0xFFU;
    CHECK(rv64::resolve_rounding_mode(0U, 0U, resolved));
    CHECK(resolved == 0U);
    CHECK(rv64::resolve_rounding_mode(7U, 3U << 5U, resolved));
    CHECK(resolved == 3U);
    CHECK(!rv64::resolve_rounding_mode(5U, 0U, resolved));
    CHECK(!rv64::resolve_rounding_mode(7U, 5U << 5U, resolved));
}

void test_core_dynamic_rounding()
{
    const std::array program{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_addi(3U << 5U, 0U, 2U),
        encode_csr(rv64::csr_address::fcsr, 2U, 0U),
        encode_u(0U, 1U, 0x17U),
        encode_flw(0x100U, 1U, 1U),
        encode_flw(0x104U, 1U, 2U),
        encode_op_fp(0x00U, 2U, 1U, 7U, 3U),
    };
    TestBus bus;
    bus.load_program(program);
    bus.put32(base + 0x110U, 0x3F800000U);
    bus.put32(base + 0x114U, 0x33800000U);

    rv64::Core core(bus);
    core.reset();
    for (std::size_t index = 0; index < program.size(); ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    CHECK(
        core.snapshot().floating_point.registers[3] ==
        rv64::box_single(0x3F800001U));
    CHECK(core.snapshot().floating_point.fcsr == 0x61U);
}

} // namespace

int main()
{
    test_decode();
    test_basic_and_fused_arithmetic();
    test_rounding_and_exceptions();
    test_core_execution_and_flag_accrual();
    test_dynamic_rounding_validation();
    test_core_dynamic_rounding();

    if (failures == 0) {
        std::cout << "All RV64 floating arithmetic M10.2 tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
