#include "rv64/core/floating.hpp"

#include <bit>

#define THREAD_LOCAL thread_local
extern "C" {
#include "softfloat.h"
}
#undef THREAD_LOCAL

namespace rv64 {

namespace {

[[nodiscard]] float32_t as_float32(std::uint64_t value) noexcept
{
    return {.v = unbox_single(value)};
}

[[nodiscard]] float64_t as_float64(std::uint64_t value) noexcept
{
    return {.v = value};
}

void begin_operation(std::uint8_t rounding_mode) noexcept
{
    softfloat_roundingMode = rounding_mode;
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = 0;
}

[[nodiscard]] FloatingResult finish(float32_t value) noexcept
{
    return {
        .value = box_single(value.v),
        .exception_flags =
            static_cast<std::uint8_t>(softfloat_exceptionFlags),
    };
}

[[nodiscard]] FloatingResult finish(float64_t value) noexcept
{
    return {
        .value = value.v,
        .exception_flags =
            static_cast<std::uint8_t>(softfloat_exceptionFlags),
    };
}

[[nodiscard]] bool is_nan32(std::uint32_t value) noexcept
{
    return (value & 0x7F800000U) == 0x7F800000U &&
           (value & 0x007FFFFFU) != 0U;
}

[[nodiscard]] bool is_signaling_nan32(std::uint32_t value) noexcept
{
    return is_nan32(value) && (value & 0x00400000U) == 0U;
}

[[nodiscard]] bool is_nan64(std::uint64_t value) noexcept
{
    return (value & 0x7FF0000000000000ULL) ==
               0x7FF0000000000000ULL &&
           (value & 0x000FFFFFFFFFFFFFULL) != 0U;
}

[[nodiscard]] bool is_signaling_nan64(std::uint64_t value) noexcept
{
    return is_nan64(value) &&
           (value & 0x0008000000000000ULL) == 0U;
}

[[nodiscard]] bool less32(
    std::uint32_t first,
    std::uint32_t second) noexcept
{
    const bool first_negative = (first >> 31U) != 0U;
    const bool second_negative = (second >> 31U) != 0U;
    if (first_negative != second_negative) {
        return first_negative;
    }
    const std::uint32_t first_magnitude = first & 0x7FFFFFFFU;
    const std::uint32_t second_magnitude = second & 0x7FFFFFFFU;
    return first_negative ? first_magnitude > second_magnitude
                          : first_magnitude < second_magnitude;
}

[[nodiscard]] bool less64(
    std::uint64_t first,
    std::uint64_t second) noexcept
{
    const bool first_negative = (first >> 63U) != 0U;
    const bool second_negative = (second >> 63U) != 0U;
    if (first_negative != second_negative) {
        return first_negative;
    }
    const std::uint64_t first_magnitude =
        first & 0x7FFFFFFFFFFFFFFFULL;
    const std::uint64_t second_magnitude =
        second & 0x7FFFFFFFFFFFFFFFULL;
    return first_negative ? first_magnitude > second_magnitude
                          : first_magnitude < second_magnitude;
}

[[nodiscard]] std::uint64_t sign_extend_word_result(
    std::uint32_t value) noexcept
{
    return (value & 0x80000000U) != 0U
               ? 0xFFFFFFFF00000000ULL | value
               : value;
}

} // namespace

bool resolve_rounding_mode(
    std::uint8_t encoded_rounding_mode,
    std::uint8_t fcsr,
    std::uint8_t& resolved_rounding_mode) noexcept
{
    std::uint8_t mode = encoded_rounding_mode;
    if (mode == 7U) {
        mode = static_cast<std::uint8_t>((fcsr >> 5U) & 0x7U);
    }
    if (mode > 4U) {
        return false;
    }
    resolved_rounding_mode = mode;
    return true;
}

FloatingResult floating_arithmetic(
    FloatingFormat format,
    FloatingArithmeticOperation operation,
    std::uint8_t rounding_mode,
    std::uint64_t first,
    std::uint64_t second,
    std::uint64_t third) noexcept
{
    begin_operation(rounding_mode);

    if (format == FloatingFormat::Single) {
        float32_t first_value = as_float32(first);
        const float32_t second_value = as_float32(second);
        float32_t third_value = as_float32(third);

        switch (operation) {
        case FloatingArithmeticOperation::Add:
            return finish(f32_add(first_value, second_value));
        case FloatingArithmeticOperation::Subtract:
            return finish(f32_sub(first_value, second_value));
        case FloatingArithmeticOperation::Multiply:
            return finish(f32_mul(first_value, second_value));
        case FloatingArithmeticOperation::Divide:
            return finish(f32_div(first_value, second_value));
        case FloatingArithmeticOperation::SquareRoot:
            return finish(f32_sqrt(first_value));
        case FloatingArithmeticOperation::MultiplyAdd:
            return finish(
                f32_mulAdd(first_value, second_value, third_value));
        case FloatingArithmeticOperation::MultiplySubtract:
            third_value.v ^= 0x80000000U;
            return finish(
                f32_mulAdd(first_value, second_value, third_value));
        case FloatingArithmeticOperation::NegatedMultiplyAdd:
            first_value.v ^= 0x80000000U;
            return finish(
                f32_mulAdd(first_value, second_value, third_value));
        case FloatingArithmeticOperation::NegatedMultiplySubtract:
            first_value.v ^= 0x80000000U;
            third_value.v ^= 0x80000000U;
            return finish(
                f32_mulAdd(first_value, second_value, third_value));
        }
    }

    float64_t first_value = as_float64(first);
    const float64_t second_value = as_float64(second);
    float64_t third_value = as_float64(third);

    switch (operation) {
    case FloatingArithmeticOperation::Add:
        return finish(f64_add(first_value, second_value));
    case FloatingArithmeticOperation::Subtract:
        return finish(f64_sub(first_value, second_value));
    case FloatingArithmeticOperation::Multiply:
        return finish(f64_mul(first_value, second_value));
    case FloatingArithmeticOperation::Divide:
        return finish(f64_div(first_value, second_value));
    case FloatingArithmeticOperation::SquareRoot:
        return finish(f64_sqrt(first_value));
    case FloatingArithmeticOperation::MultiplyAdd:
        return finish(
            f64_mulAdd(first_value, second_value, third_value));
    case FloatingArithmeticOperation::MultiplySubtract:
        third_value.v ^= 0x8000000000000000ULL;
        return finish(
            f64_mulAdd(first_value, second_value, third_value));
    case FloatingArithmeticOperation::NegatedMultiplyAdd:
        first_value.v ^= 0x8000000000000000ULL;
        return finish(
            f64_mulAdd(first_value, second_value, third_value));
    case FloatingArithmeticOperation::NegatedMultiplySubtract:
        first_value.v ^= 0x8000000000000000ULL;
        third_value.v ^= 0x8000000000000000ULL;
        return finish(
            f64_mulAdd(first_value, second_value, third_value));
    }

    return {};
}

std::uint64_t floating_sign_injection(
    FloatingFormat format,
    FloatingSignOperation operation,
    std::uint64_t first,
    std::uint64_t second) noexcept
{
    if (format == FloatingFormat::Single) {
        const std::uint32_t first_value = unbox_single(first);
        const std::uint32_t second_value = unbox_single(second);
        std::uint32_t sign = second_value & 0x80000000U;
        if (operation == FloatingSignOperation::Negate) {
            sign ^= 0x80000000U;
        } else if (operation == FloatingSignOperation::ExclusiveOr) {
            sign = (first_value ^ second_value) & 0x80000000U;
        }
        return box_single((first_value & 0x7FFFFFFFU) | sign);
    }

    std::uint64_t sign = second & 0x8000000000000000ULL;
    if (operation == FloatingSignOperation::Negate) {
        sign ^= 0x8000000000000000ULL;
    } else if (operation == FloatingSignOperation::ExclusiveOr) {
        sign = (first ^ second) & 0x8000000000000000ULL;
    }
    return (first & 0x7FFFFFFFFFFFFFFFULL) | sign;
}

FloatingResult floating_min_max(
    FloatingFormat format,
    FloatingMinMaxOperation operation,
    std::uint64_t first,
    std::uint64_t second) noexcept
{
    if (format == FloatingFormat::Single) {
        const std::uint32_t first_value = unbox_single(first);
        const std::uint32_t second_value = unbox_single(second);
        const bool first_nan = is_nan32(first_value);
        const bool second_nan = is_nan32(second_value);
        const std::uint8_t flags =
            is_signaling_nan32(first_value) ||
                    is_signaling_nan32(second_value)
                ? 0x10U
                : 0U;
        if (first_nan && second_nan) {
            return {
                .value = box_single(canonical_nan32),
                .exception_flags = flags,
            };
        }
        if (first_nan) {
            return {
                .value = box_single(second_value),
                .exception_flags = flags,
            };
        }
        if (second_nan) {
            return {
                .value = box_single(first_value),
                .exception_flags = flags,
            };
        }
        if ((first_value & 0x7FFFFFFFU) == 0U &&
            (second_value & 0x7FFFFFFFU) == 0U) {
            const std::uint32_t value =
                operation == FloatingMinMaxOperation::Minimum
                    ? first_value | second_value
                    : first_value & second_value;
            return {.value = box_single(value)};
        }
        const bool first_is_less = less32(first_value, second_value);
        const std::uint32_t value =
            (operation == FloatingMinMaxOperation::Minimum)
                ? (first_is_less ? first_value : second_value)
                : (first_is_less ? second_value : first_value);
        return {.value = box_single(value)};
    }

    const bool first_nan = is_nan64(first);
    const bool second_nan = is_nan64(second);
    const std::uint8_t flags =
        is_signaling_nan64(first) || is_signaling_nan64(second)
            ? 0x10U
            : 0U;
    if (first_nan && second_nan) {
        return {
            .value = canonical_nan64,
            .exception_flags = flags,
        };
    }
    if (first_nan) {
        return {.value = second, .exception_flags = flags};
    }
    if (second_nan) {
        return {.value = first, .exception_flags = flags};
    }
    if ((first & 0x7FFFFFFFFFFFFFFFULL) == 0U &&
        (second & 0x7FFFFFFFFFFFFFFFULL) == 0U) {
        const std::uint64_t value =
            operation == FloatingMinMaxOperation::Minimum
                ? first | second
                : first & second;
        return {.value = value};
    }
    const bool first_is_less = less64(first, second);
    return {
        .value =
            operation == FloatingMinMaxOperation::Minimum
                ? (first_is_less ? first : second)
                : (first_is_less ? second : first),
    };
}

FloatingIntegerResult floating_compare(
    FloatingFormat format,
    FloatingComparisonOperation operation,
    std::uint64_t first,
    std::uint64_t second) noexcept
{
    begin_operation(0U);
    bool result = false;
    if (format == FloatingFormat::Single) {
        const float32_t first_value = as_float32(first);
        const float32_t second_value = as_float32(second);
        if (operation == FloatingComparisonOperation::Equal) {
            result = f32_eq(first_value, second_value);
        } else if (operation == FloatingComparisonOperation::LessThan) {
            result = f32_lt(first_value, second_value);
        } else {
            result = f32_le(first_value, second_value);
        }
    } else {
        const float64_t first_value = as_float64(first);
        const float64_t second_value = as_float64(second);
        if (operation == FloatingComparisonOperation::Equal) {
            result = f64_eq(first_value, second_value);
        } else if (operation == FloatingComparisonOperation::LessThan) {
            result = f64_lt(first_value, second_value);
        } else {
            result = f64_le(first_value, second_value);
        }
    }
    return {
        .value = result ? 1U : 0U,
        .exception_flags =
            static_cast<std::uint8_t>(softfloat_exceptionFlags),
    };
}

std::uint16_t floating_classify(
    FloatingFormat format,
    std::uint64_t value) noexcept
{
    if (format == FloatingFormat::Single) {
        const std::uint32_t bits = unbox_single(value);
        const bool negative = (bits >> 31U) != 0U;
        const std::uint32_t exponent = (bits >> 23U) & 0xFFU;
        const std::uint32_t fraction = bits & 0x7FFFFFU;
        if (exponent == 0xFFU) {
            if (fraction == 0U) {
                return static_cast<std::uint16_t>(
                    std::uint16_t{1} << (negative ? 0U : 7U));
            }
            return static_cast<std::uint16_t>(
                std::uint16_t{1} <<
                (is_signaling_nan32(bits) ? 8U : 9U));
        }
        if (exponent == 0U) {
            return static_cast<std::uint16_t>(
                std::uint16_t{1} <<
                (fraction == 0U
                     ? (negative ? 3U : 4U)
                     : (negative ? 2U : 5U)));
        }
        return static_cast<std::uint16_t>(
            std::uint16_t{1} << (negative ? 1U : 6U));
    }

    const bool negative = (value >> 63U) != 0U;
    const std::uint64_t exponent = (value >> 52U) & 0x7FFU;
    const std::uint64_t fraction = value & 0xFFFFFFFFFFFFFULL;
    if (exponent == 0x7FFU) {
        if (fraction == 0U) {
            return static_cast<std::uint16_t>(
                std::uint16_t{1} << (negative ? 0U : 7U));
        }
        return static_cast<std::uint16_t>(
            std::uint16_t{1} <<
            (is_signaling_nan64(value) ? 8U : 9U));
    }
    if (exponent == 0U) {
        return static_cast<std::uint16_t>(
            std::uint16_t{1} <<
            (fraction == 0U
                 ? (negative ? 3U : 4U)
                 : (negative ? 2U : 5U)));
    }
    return static_cast<std::uint16_t>(
        std::uint16_t{1} << (negative ? 1U : 6U));
}

FloatingResult floating_convert_format(
    FloatingFormat destination_format,
    std::uint8_t rounding_mode,
    std::uint64_t source) noexcept
{
    begin_operation(rounding_mode);
    if (destination_format == FloatingFormat::Single) {
        return finish(f64_to_f32(as_float64(source)));
    }
    return finish(f32_to_f64(as_float32(source)));
}

FloatingIntegerResult floating_to_integer(
    FloatingFormat source_format,
    FloatingIntegerWidth width,
    bool unsigned_integer,
    std::uint8_t rounding_mode,
    std::uint64_t source) noexcept
{
    begin_operation(rounding_mode);
    std::uint64_t value = 0;
    if (source_format == FloatingFormat::Single) {
        const float32_t source_value = as_float32(source);
        if (width == FloatingIntegerWidth::Word) {
            const std::uint32_t word =
                unsigned_integer
                    ? static_cast<std::uint32_t>(
                          f32_to_ui32(
                              source_value,
                              rounding_mode,
                              true))
                    : static_cast<std::uint32_t>(
                          f32_to_i32(
                              source_value,
                              rounding_mode,
                              true));
            value = sign_extend_word_result(word);
        } else {
            value = unsigned_integer
                        ? static_cast<std::uint64_t>(
                              f32_to_ui64(
                                  source_value,
                                  rounding_mode,
                                  true))
                        : static_cast<std::uint64_t>(
                              f32_to_i64(
                                  source_value,
                                  rounding_mode,
                                  true));
        }
    } else {
        const float64_t source_value = as_float64(source);
        if (width == FloatingIntegerWidth::Word) {
            const std::uint32_t word =
                unsigned_integer
                    ? static_cast<std::uint32_t>(
                          f64_to_ui32(
                              source_value,
                              rounding_mode,
                              true))
                    : static_cast<std::uint32_t>(
                          f64_to_i32(
                              source_value,
                              rounding_mode,
                              true));
            value = sign_extend_word_result(word);
        } else {
            value = unsigned_integer
                        ? static_cast<std::uint64_t>(
                              f64_to_ui64(
                                  source_value,
                                  rounding_mode,
                                  true))
                        : static_cast<std::uint64_t>(
                              f64_to_i64(
                                  source_value,
                                  rounding_mode,
                                  true));
        }
    }
    return {
        .value = value,
        .exception_flags =
            static_cast<std::uint8_t>(softfloat_exceptionFlags),
    };
}

FloatingResult integer_to_floating(
    FloatingFormat destination_format,
    FloatingIntegerWidth width,
    bool unsigned_integer,
    std::uint8_t rounding_mode,
    std::uint64_t source) noexcept
{
    begin_operation(rounding_mode);
    if (destination_format == FloatingFormat::Single) {
        if (width == FloatingIntegerWidth::Word) {
            const std::uint32_t word = static_cast<std::uint32_t>(source);
            return unsigned_integer
                       ? finish(ui32_to_f32(word))
                       : finish(i32_to_f32(
                             std::bit_cast<std::int32_t>(word)));
        }
        return unsigned_integer
                   ? finish(ui64_to_f32(source))
                   : finish(i64_to_f32(
                         std::bit_cast<std::int64_t>(source)));
    }
    if (width == FloatingIntegerWidth::Word) {
        const std::uint32_t word = static_cast<std::uint32_t>(source);
        return unsigned_integer
                   ? finish(ui32_to_f64(word))
                   : finish(i32_to_f64(
                         std::bit_cast<std::int32_t>(word)));
    }
    return unsigned_integer
               ? finish(ui64_to_f64(source))
               : finish(i64_to_f64(
                     std::bit_cast<std::int64_t>(source)));
}

} // namespace rv64
