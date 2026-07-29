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

[[nodiscard]] constexpr std::uint32_t with_registers(
    std::uint32_t match,
    std::uint32_t rs2,
    std::uint32_t rs1,
    std::uint32_t rd)
{
    return match |
           ((rs2 & 0x1FU) << 20U) |
           ((rs1 & 0x1FU) << 15U) |
           ((rd & 0x1FU) << 7U);
}

constexpr std::array<std::uint32_t, 2> enable_floating_point{
    encode_u(0x2000U, 1U, 0x37U),
    encode_csr(rv64::csr_address::mstatus, 1U, 0U),
};

void test_decode()
{
    using K = rv64::InstructionKind;
    struct Case {
        std::uint32_t match;
        K kind;
    };
    constexpr std::array cases{
        Case{0x20000053U, K::FsgnjS},
        Case{0x20001053U, K::FsgnjnS},
        Case{0x20002053U, K::FsgnjxS},
        Case{0x22000053U, K::FsgnjD},
        Case{0x22001053U, K::FsgnjnD},
        Case{0x22002053U, K::FsgnjxD},
        Case{0x28000053U, K::FminS},
        Case{0x28001053U, K::FmaxS},
        Case{0x2A000053U, K::FminD},
        Case{0x2A001053U, K::FmaxD},
        Case{0xA0002053U, K::FeqS},
        Case{0xA0001053U, K::FltS},
        Case{0xA0000053U, K::FleS},
        Case{0xA2002053U, K::FeqD},
        Case{0xA2001053U, K::FltD},
        Case{0xA2000053U, K::FleD},
        Case{0xE0001053U, K::FclassS},
        Case{0xE2001053U, K::FclassD},
        Case{0x40100053U, K::FcvtSD},
        Case{0x42000053U, K::FcvtDS},
        Case{0xC0000053U, K::FcvtWS},
        Case{0xC0100053U, K::FcvtWuS},
        Case{0xC0200053U, K::FcvtLS},
        Case{0xC0300053U, K::FcvtLuS},
        Case{0xC2000053U, K::FcvtWD},
        Case{0xC2100053U, K::FcvtWuD},
        Case{0xC2200053U, K::FcvtLD},
        Case{0xC2300053U, K::FcvtLuD},
        Case{0xD0000053U, K::FcvtSW},
        Case{0xD0100053U, K::FcvtSWu},
        Case{0xD0200053U, K::FcvtSL},
        Case{0xD0300053U, K::FcvtSLu},
        Case{0xD2000053U, K::FcvtDW},
        Case{0xD2100053U, K::FcvtDWu},
        Case{0xD2200053U, K::FcvtDL},
        Case{0xD2300053U, K::FcvtDLu},
    };

    for (const auto& test : cases) {
        const bool classification =
            test.kind == K::FclassS || test.kind == K::FclassD;
        const bool binary =
            test.kind <= K::FleD;
        const std::uint32_t rs2 =
            binary && !classification ? 2U : 0U;
        const auto decoded = rv64::decode_instruction(
            with_registers(test.match, rs2, 1U, 3U));
        CHECK(decoded.kind == test.kind);
        CHECK(decoded.rd == 3U);
        CHECK(decoded.rs1 == 1U);
    }

    CHECK(
        !rv64::decode_instruction(
             with_registers(0xC0005053U, 0U, 1U, 3U))
             .valid());
    CHECK(
        !rv64::decode_instruction(
             with_registers(0x20003053U, 2U, 1U, 3U))
             .valid());
}

void test_sign_min_max_and_compare()
{
    using F = rv64::FloatingFormat;

    CHECK(
        rv64::floating_sign_injection(
            F::Single,
            rv64::FloatingSignOperation::Copy,
            rv64::box_single(0x3F800000U),
            rv64::box_single(0x80000000U)) ==
        rv64::box_single(0xBF800000U));
    CHECK(
        rv64::floating_sign_injection(
            F::Double,
            rv64::FloatingSignOperation::ExclusiveOr,
            0xBFF0000000000000ULL,
            0xC000000000000000ULL) ==
        0x3FF0000000000000ULL);

    const auto minimum_zero = rv64::floating_min_max(
        F::Single,
        rv64::FloatingMinMaxOperation::Minimum,
        rv64::box_single(0x00000000U),
        rv64::box_single(0x80000000U));
    CHECK(minimum_zero.value == rv64::box_single(0x80000000U));
    const auto maximum_zero = rv64::floating_min_max(
        F::Single,
        rv64::FloatingMinMaxOperation::Maximum,
        rv64::box_single(0x00000000U),
        rv64::box_single(0x80000000U));
    CHECK(maximum_zero.value == rv64::box_single(0x00000000U));

    const auto one_nan = rv64::floating_min_max(
        F::Double,
        rv64::FloatingMinMaxOperation::Minimum,
        rv64::canonical_nan64,
        0x4000000000000000ULL);
    CHECK(one_nan.value == 0x4000000000000000ULL);
    CHECK(one_nan.exception_flags == 0U);

    const auto signaling_nan = rv64::floating_min_max(
        F::Double,
        rv64::FloatingMinMaxOperation::Maximum,
        0x7FF0000000000001ULL,
        0x4000000000000000ULL);
    CHECK(signaling_nan.value == 0x4000000000000000ULL);
    CHECK(signaling_nan.exception_flags == 0x10U);

    const auto equal_nan = rv64::floating_compare(
        F::Single,
        rv64::FloatingComparisonOperation::Equal,
        rv64::box_single(rv64::canonical_nan32),
        rv64::box_single(0x3F800000U));
    CHECK(equal_nan.value == 0U);
    CHECK(equal_nan.exception_flags == 0U);

    const auto less_nan = rv64::floating_compare(
        F::Single,
        rv64::FloatingComparisonOperation::LessThan,
        rv64::box_single(rv64::canonical_nan32),
        rv64::box_single(0x3F800000U));
    CHECK(less_nan.value == 0U);
    CHECK(less_nan.exception_flags == 0x10U);

    const auto normal_compare = rv64::floating_compare(
        F::Double,
        rv64::FloatingComparisonOperation::LessOrEqual,
        0xBFF0000000000000ULL,
        0x0000000000000000ULL);
    CHECK(normal_compare.value == 1U);
    CHECK(normal_compare.exception_flags == 0U);
}

void test_classification()
{
    using F = rv64::FloatingFormat;
    constexpr std::array<std::uint32_t, 10> single_values{
        0xFF800000U,
        0xBF800000U,
        0x80000001U,
        0x80000000U,
        0x00000000U,
        0x00000001U,
        0x3F800000U,
        0x7F800000U,
        0x7F800001U,
        0x7FC00000U,
    };
    for (std::uint16_t index = 0; index < single_values.size(); ++index) {
        CHECK(
            rv64::floating_classify(
                F::Single,
                rv64::box_single(single_values[index])) ==
            static_cast<std::uint16_t>(1U << index));
    }

    CHECK(
        rv64::floating_classify(
            F::Single,
            0x000000003F800000ULL) ==
        (1U << 9U));
    CHECK(
        rv64::floating_classify(
            F::Double,
            0xFFF0000000000000ULL) ==
        (1U << 0U));
    CHECK(
        rv64::floating_classify(
            F::Double,
            0x7FF8000000000000ULL) ==
        (1U << 9U));
}

void test_format_and_integer_conversions()
{
    using F = rv64::FloatingFormat;
    using W = rv64::FloatingIntegerWidth;

    const auto to_double = rv64::floating_convert_format(
        F::Double,
        0U,
        rv64::box_single(0x3FC00000U));
    CHECK(to_double.value == 0x3FF8000000000000ULL);
    CHECK(to_double.exception_flags == 0U);

    const auto to_single = rv64::floating_convert_format(
        F::Single,
        0U,
        0x3FF199999999999AULL);
    CHECK(to_single.value == rv64::box_single(0x3F8CCCCDU));
    CHECK(to_single.exception_flags == 0x01U);

    const auto word_nearest = rv64::floating_to_integer(
        F::Single,
        W::Word,
        false,
        0U,
        rv64::box_single(0x3FC00000U));
    CHECK(word_nearest.value == 2U);
    CHECK(word_nearest.exception_flags == 0x01U);

    const auto word_truncate = rv64::floating_to_integer(
        F::Single,
        W::Word,
        false,
        1U,
        rv64::box_single(0x3FC00000U));
    CHECK(word_truncate.value == 1U);
    CHECK(word_truncate.exception_flags == 0x01U);

    const auto negative_infinity = rv64::floating_to_integer(
        F::Double,
        W::Word,
        false,
        0U,
        0xFFF0000000000000ULL);
    CHECK(negative_infinity.value == 0xFFFFFFFF80000000ULL);
    CHECK(negative_infinity.exception_flags == 0x10U);

    const auto unsigned_negative = rv64::floating_to_integer(
        F::Double,
        W::Long,
        true,
        0U,
        0xBFF0000000000000ULL);
    CHECK(unsigned_negative.value == 0U);
    CHECK(unsigned_negative.exception_flags == 0x10U);

    const auto unsigned_word_max = rv64::floating_to_integer(
        F::Double,
        W::Word,
        true,
        0U,
        0x41EFFFFFFFE00000ULL);
    CHECK(unsigned_word_max.value == 0xFFFFFFFFFFFFFFFFULL);
    CHECK(unsigned_word_max.exception_flags == 0U);

    const auto signed_word = rv64::integer_to_floating(
        F::Double,
        W::Word,
        false,
        0U,
        0xFFFFFFFFFFFFFFFFULL);
    CHECK(signed_word.value == 0xBFF0000000000000ULL);
    CHECK(signed_word.exception_flags == 0U);

    const auto rounded_long = rv64::integer_to_floating(
        F::Single,
        W::Long,
        true,
        0U,
        0x0000000001000001ULL);
    CHECK(rounded_long.value == rv64::box_single(0x4B800000U));
    CHECK(rounded_long.exception_flags == 0x01U);

    const auto rounded_long_up = rv64::integer_to_floating(
        F::Single,
        W::Long,
        true,
        3U,
        0x0000000001000001ULL);
    CHECK(rounded_long_up.value == rv64::box_single(0x4B800001U));
    CHECK(rounded_long_up.exception_flags == 0x01U);
}

void test_core_integration()
{
    const std::array program{
        enable_floating_point[0],
        enable_floating_point[1],
        encode_u(0U, 1U, 0x17U),
        encode_flw(0x100U, 1U, 1U),
        with_registers(0xC0001053U, 0U, 1U, 2U),
        with_registers(0xE0001053U, 0U, 1U, 3U),
        with_registers(0x20001053U, 1U, 1U, 2U),
        with_registers(0xA0002053U, 1U, 1U, 4U),
    };
    TestBus bus;
    bus.load_program(program);
    bus.put32(base + 0x108U, 0x3FC00000U);

    rv64::Core core(bus);
    core.reset();
    for (std::size_t index = 0; index < program.size(); ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }

    CHECK(core.snapshot().registers[2] == 1U);
    CHECK(core.snapshot().registers[3] == (1U << 6U));
    CHECK(core.snapshot().registers[4] == 1U);
    CHECK(
        core.snapshot().floating_point.registers[2] ==
        rv64::box_single(0xBFC00000U));
    CHECK((core.snapshot().floating_point.fcsr & 0x1FU) == 0x01U);
}

} // namespace

int main()
{
    test_decode();
    test_sign_min_max_and_compare();
    test_classification();
    test_format_and_integer_conversions();
    test_core_integration();

    if (failures == 0) {
        std::cout << "All RV64 floating conversion M10.3 tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
