#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "rv/common/bus.hpp"

namespace rv::platform {

[[nodiscard]] inline ReadResult read_little_endian(
    std::span<const std::uint8_t> bytes,
    std::uint64_t offset,
    AccessWidth width) noexcept
{
    const auto count = static_cast<std::uint64_t>(width_bytes(width));
    const auto available = static_cast<std::uint64_t>(bytes.size());
    if (count == 0 || count > available ||
        offset > available - count) {
        return {.fault = BusFault::OutOfRange};
    }

    std::uint64_t value = 0;
    for (std::uint64_t index = 0; index < count; ++index) {
        const auto byte_index = static_cast<std::size_t>(offset + index);
        const auto shift = static_cast<unsigned int>(index * 8U);
        value |= static_cast<std::uint64_t>(bytes[byte_index]) << shift;
    }

    return {.fault = BusFault::None, .value = value};
}

[[nodiscard]] inline BusFault write_little_endian(
    std::span<std::uint8_t> bytes,
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value) noexcept
{
    const auto count = static_cast<std::uint64_t>(width_bytes(width));
    const auto available = static_cast<std::uint64_t>(bytes.size());
    if (count == 0 || count > available ||
        offset > available - count) {
        return BusFault::OutOfRange;
    }

    for (std::uint64_t index = 0; index < count; ++index) {
        const auto byte_index = static_cast<std::size_t>(offset + index);
        const auto shift = static_cast<unsigned int>(index * 8U);
        bytes[byte_index] =
            static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }

    return BusFault::None;
}

} // namespace rv::platform
