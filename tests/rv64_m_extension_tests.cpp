#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>

#include "rv/common/bus.hpp"
#include "rv64/core/core.hpp"
#include "rv64/core/decode.hpp"

namespace {

constexpr std::uint64_t base = 0x80000000ULL;
constexpr std::size_t memory_size = 8192U;
constexpr std::size_t data_offset = 4096U;
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
        if (address < base ||
            address - base > bytes_.size() - count) {
            return {.fault = rv::BusFault::Unmapped};
        }
        const std::size_t offset =
            static_cast<std::size_t>(address - base);
        std::uint64_t value = 0;
        for (std::size_t index = 0;
             index < static_cast<std::size_t>(count);
             ++index) {
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

    void prepare(
        std::uint32_t operation,
        std::uint64_t lhs,
        std::uint64_t rhs)
    {
        bytes_.fill(0);
        constexpr std::array<std::uint32_t, 4> prefix{
            0x00001F17U, // auipc x30,0x1000
            0x000F3083U, // ld x1,0(x30)
            0x008F3103U, // ld x2,8(x30)
            0U,
        };
        for (std::size_t index = 0; index < prefix.size(); ++index) {
            const std::uint32_t instruction =
                index == 3U ? operation : prefix[index];
            write_value(index * 4U, instruction, 4U);
        }
        write_value(data_offset, lhs, 8U);
        write_value(data_offset + 8U, rhs, 8U);
    }

  private:
    void write_value(
        std::size_t offset,
        std::uint64_t value,
        std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index) {
            bytes_[offset + index] = static_cast<std::uint8_t>(
                value >> (index * 8U));
        }
    }

    std::array<std::uint8_t, memory_size> bytes_{};
};

[[nodiscard]] constexpr std::uint32_t encode_m(
    std::uint32_t funct3,
    bool word = false)
{
    return (1U << 25U) |
           (2U << 20U) |
           (1U << 15U) |
           ((funct3 & 7U) << 12U) |
           (3U << 7U) |
           (word ? 0x3BU : 0x33U);
}

[[nodiscard]] std::uint64_t execute(
    TestBus& bus,
    std::uint32_t instruction,
    std::uint64_t lhs,
    std::uint64_t rhs)
{
    bus.prepare(instruction, lhs, rhs);
    rv64::Core core(bus);
    core.reset({.reset_pc = base});
    for (int index = 0; index < 4; ++index) {
        CHECK(core.step().status == rv64::StepStatus::Retired);
    }
    return core.snapshot().registers[3];
}

void test_decode()
{
    using K = rv64::InstructionKind;
    constexpr K expected[]{
        K::Mul, K::Mulh, K::Mulhsu, K::Mulhu,
        K::Div, K::Divu, K::Rem, K::Remu,
    };
    for (std::uint32_t funct3 = 0; funct3 < 8U; ++funct3) {
        CHECK(
            rv64::decode_instruction(encode_m(funct3)).kind ==
            expected[funct3]);
    }
    CHECK(rv64::decode_instruction(encode_m(0U, true)).kind == K::Mulw);
    CHECK(rv64::decode_instruction(encode_m(4U, true)).kind == K::Divw);
    CHECK(rv64::decode_instruction(encode_m(5U, true)).kind == K::Divuw);
    CHECK(rv64::decode_instruction(encode_m(6U, true)).kind == K::Remw);
    CHECK(rv64::decode_instruction(encode_m(7U, true)).kind == K::Remuw);
    CHECK(!rv64::decode_instruction(encode_m(1U, true)).valid());
}

void test_multiply()
{
    TestBus bus;
    CHECK(
        execute(
            bus,
            encode_m(0U),
            0x123456789ABCDEF0ULL,
            0x10ULL) ==
        0x23456789ABCDEF00ULL);
    CHECK(
        execute(
            bus,
            encode_m(1U),
            std::uint64_t{1} << 63U,
            2U) ==
        std::numeric_limits<std::uint64_t>::max());
    CHECK(
        execute(
            bus,
            encode_m(2U),
            std::numeric_limits<std::uint64_t>::max(),
            std::numeric_limits<std::uint64_t>::max()) ==
        std::numeric_limits<std::uint64_t>::max());
    CHECK(
        execute(
            bus,
            encode_m(3U),
            std::numeric_limits<std::uint64_t>::max(),
            std::numeric_limits<std::uint64_t>::max()) ==
        std::numeric_limits<std::uint64_t>::max() - 1U);
    CHECK(
        execute(bus, encode_m(0U, true), 0xFFFFFFFFULL, 2U) ==
        0xFFFFFFFFFFFFFFFEULL);
}

void test_divide_and_remainder()
{
    TestBus bus;
    constexpr std::uint64_t minus20 = 0xFFFFFFFFFFFFFFECULL;
    CHECK(execute(bus, encode_m(4U), minus20, 3U) ==
          0xFFFFFFFFFFFFFFFAULL);
    CHECK(execute(bus, encode_m(6U), minus20, 3U) ==
          0xFFFFFFFFFFFFFFFEULL);
    CHECK(
        execute(
            bus,
            encode_m(5U),
            std::numeric_limits<std::uint64_t>::max(),
            2U) ==
        0x7FFFFFFFFFFFFFFFULL);
    CHECK(
        execute(
            bus,
            encode_m(7U),
            std::numeric_limits<std::uint64_t>::max(),
            2U) == 1U);

    const std::uint64_t minimum = std::uint64_t{1} << 63U;
    const std::uint64_t minus1 =
        std::numeric_limits<std::uint64_t>::max();
    CHECK(execute(bus, encode_m(4U), minimum, minus1) == minimum);
    CHECK(execute(bus, encode_m(6U), minimum, minus1) == 0U);
    CHECK(execute(bus, encode_m(4U), 123U, 0U) == minus1);
    CHECK(execute(bus, encode_m(5U), 123U, 0U) == minus1);
    CHECK(execute(bus, encode_m(6U), 123U, 0U) == 123U);
    CHECK(execute(bus, encode_m(7U), 123U, 0U) == 123U);
}

void test_word_operations()
{
    TestBus bus;
    CHECK(execute(bus, encode_m(4U, true), 0xFFFFFFECU, 3U) ==
          0xFFFFFFFFFFFFFFFAULL);
    CHECK(execute(bus, encode_m(5U, true), 0xFFFFFFFFU, 2U) ==
          0x000000007FFFFFFFULL);
    CHECK(execute(bus, encode_m(6U, true), 0xFFFFFFECU, 3U) ==
          0xFFFFFFFFFFFFFFFEULL);
    CHECK(execute(bus, encode_m(7U, true), 0xFFFFFFFFU, 2U) == 1U);

    CHECK(execute(bus, encode_m(4U, true), 0x80000000U, 0xFFFFFFFFU) ==
          0xFFFFFFFF80000000ULL);
    CHECK(execute(bus, encode_m(6U, true), 0x80000000U, 0xFFFFFFFFU) == 0U);
    CHECK(execute(bus, encode_m(4U, true), 7U, 0U) ==
          std::numeric_limits<std::uint64_t>::max());
    CHECK(execute(bus, encode_m(5U, true), 7U, 0U) ==
          std::numeric_limits<std::uint64_t>::max());
    CHECK(execute(bus, encode_m(6U, true), 0x80000000U, 0U) ==
          0xFFFFFFFF80000000ULL);
    CHECK(execute(bus, encode_m(7U, true), 0x80000000U, 0U) ==
          0xFFFFFFFF80000000ULL);
}

} // namespace

int main()
{
    test_decode();
    test_multiply();
    test_divide_and_remainder();
    test_word_operations();
    if (failures == 0) {
        std::cout << "All independent RV64M tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
