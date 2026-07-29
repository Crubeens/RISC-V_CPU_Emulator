#pragma once

#include <cstdint>

namespace rv64 {

enum class FloatingFormat : std::uint8_t {
    Single,
    Double,
};

enum class FloatingArithmeticOperation : std::uint8_t {
    Add,
    Subtract,
    Multiply,
    Divide,
    SquareRoot,
    MultiplyAdd,
    MultiplySubtract,
    NegatedMultiplyAdd,
    NegatedMultiplySubtract,
};

enum class FloatingSignOperation : std::uint8_t {
    Copy,
    Negate,
    ExclusiveOr,
};

enum class FloatingMinMaxOperation : std::uint8_t {
    Minimum,
    Maximum,
};

enum class FloatingComparisonOperation : std::uint8_t {
    Equal,
    LessThan,
    LessOrEqual,
};

enum class FloatingIntegerWidth : std::uint8_t {
    Word,
    Long,
};

struct FloatingResult {
    std::uint64_t value{};
    std::uint8_t exception_flags{};
};

struct FloatingIntegerResult {
    std::uint64_t value{};
    std::uint8_t exception_flags{};
};

inline constexpr std::uint64_t single_nan_box =
    0xFFFFFFFF00000000ULL;
inline constexpr std::uint32_t canonical_nan32 =
    0x7FC00000U;
inline constexpr std::uint64_t canonical_nan64 =
    0x7FF8000000000000ULL;

[[nodiscard]] constexpr std::uint32_t unbox_single(
    std::uint64_t value) noexcept
{
    return (value & single_nan_box) == single_nan_box
               ? static_cast<std::uint32_t>(value)
               : canonical_nan32;
}

[[nodiscard]] constexpr std::uint64_t box_single(
    std::uint32_t value) noexcept
{
    return single_nan_box | value;
}

[[nodiscard]] bool resolve_rounding_mode(
    std::uint8_t encoded_rounding_mode,
    std::uint8_t fcsr,
    std::uint8_t& resolved_rounding_mode) noexcept;

[[nodiscard]] FloatingResult floating_arithmetic(
    FloatingFormat format,
    FloatingArithmeticOperation operation,
    std::uint8_t rounding_mode,
    std::uint64_t first,
    std::uint64_t second = 0,
    std::uint64_t third = 0) noexcept;

[[nodiscard]] std::uint64_t floating_sign_injection(
    FloatingFormat format,
    FloatingSignOperation operation,
    std::uint64_t first,
    std::uint64_t second) noexcept;

[[nodiscard]] FloatingResult floating_min_max(
    FloatingFormat format,
    FloatingMinMaxOperation operation,
    std::uint64_t first,
    std::uint64_t second) noexcept;

[[nodiscard]] FloatingIntegerResult floating_compare(
    FloatingFormat format,
    FloatingComparisonOperation operation,
    std::uint64_t first,
    std::uint64_t second) noexcept;

[[nodiscard]] std::uint16_t floating_classify(
    FloatingFormat format,
    std::uint64_t value) noexcept;

[[nodiscard]] FloatingResult floating_convert_format(
    FloatingFormat destination_format,
    std::uint8_t rounding_mode,
    std::uint64_t source) noexcept;

[[nodiscard]] FloatingIntegerResult floating_to_integer(
    FloatingFormat source_format,
    FloatingIntegerWidth width,
    bool unsigned_integer,
    std::uint8_t rounding_mode,
    std::uint64_t source) noexcept;

[[nodiscard]] FloatingResult integer_to_floating(
    FloatingFormat destination_format,
    FloatingIntegerWidth width,
    bool unsigned_integer,
    std::uint8_t rounding_mode,
    std::uint64_t source) noexcept;

} // namespace rv64
