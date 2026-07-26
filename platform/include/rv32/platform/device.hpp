#pragma once

#include <cstdint>
#include <string_view>

#include "rv32/core/bus.hpp"

namespace rv32::platform {

struct AddressRange {
    PhysAddr base{};
    std::uint64_t size{};

    [[nodiscard]] constexpr PhysAddr end_exclusive() const noexcept
    {
        return base + size;
    }

    [[nodiscard]] constexpr bool contains(
        PhysAddr address,
        AccessWidth width) const noexcept
    {
        const auto bytes = static_cast<std::uint64_t>(width_bytes(width));
        if (size == 0 || bytes == 0 || bytes > size || address < base) {
            return false;
        }

        const auto offset = address - base;
        return offset <= size - bytes;
    }

    [[nodiscard]] constexpr bool overlaps(
        const AddressRange& other) const noexcept
    {
        if (size == 0 || other.size == 0) {
            return false;
        }
        return base < other.end_exclusive() &&
               other.base < end_exclusive();
    }
};

class DmaAccess {
  public:
    virtual ~DmaAccess() = default;

    [[nodiscard]] virtual ReadResult dma_read(
        PhysAddr address,
        AccessWidth width) = 0;

    [[nodiscard]] virtual BusFault dma_write(
        PhysAddr address,
        AccessWidth width,
        std::uint64_t value) = 0;
};

class TimeSource {
  public:
    virtual ~TimeSource() = default;
    [[nodiscard]] virtual std::uint64_t time_value() const noexcept = 0;
};

class Device {
  public:
    virtual ~Device() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual AddressRange range() const noexcept = 0;

    [[nodiscard]] virtual ReadResult read(
        std::uint64_t offset,
        AccessWidth width) = 0;

    [[nodiscard]] virtual BusFault write(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t value) = 0;

    virtual void tick(DmaAccess& dma, std::uint64_t cycles)
    {
        static_cast<void>(dma);
        static_cast<void>(cycles);
    }
};

} // namespace rv32::platform
