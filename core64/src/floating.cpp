#include "rv64/core/floating.hpp"

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

} // namespace rv64
