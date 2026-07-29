#include "rv/devices/clint.hpp"

#include <limits>

namespace rv::devices {

Clint::Clint(PhysAddr base, std::uint64_t size)
    : range_{.base = base, .size = size}
{
}

std::string_view Clint::name() const noexcept
{
    return "CLINT";
}

platform::AddressRange Clint::range() const noexcept
{
    return range_;
}

ReadResult Clint::read(
    std::uint64_t offset,
    AccessWidth width)
{
    const auto count = static_cast<std::uint64_t>(width_bytes(width));

    if (offset >= msip_offset &&
        offset < msip_offset + sizeof(msip_)) {
        return read_register(
            msip_,
            sizeof(msip_),
            offset - msip_offset,
            width);
    }

    if (offset >= mtimecmp_offset &&
        offset < mtimecmp_offset + sizeof(mtimecmp_)) {
        return read_register(
            mtimecmp_,
            sizeof(mtimecmp_),
            offset - mtimecmp_offset,
            width);
    }

    if (offset >= mtime_offset &&
        offset < mtime_offset + sizeof(mtime_)) {
        return read_register(
            mtime_,
            sizeof(mtime_),
            offset - mtime_offset,
            width);
    }

    static_cast<void>(count);
    return {.fault = BusFault::None, .value = 0};
}

BusFault Clint::write(
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value)
{
    if (offset >= msip_offset &&
        offset < msip_offset + sizeof(msip_)) {
        auto register_value = static_cast<std::uint64_t>(msip_);
        const auto fault = write_register(
            register_value,
            sizeof(msip_),
            offset - msip_offset,
            width,
            value);
        if (fault == BusFault::None) {
            msip_ = static_cast<std::uint32_t>(register_value) & 0x1U;
        }
        return fault;
    }

    if (offset >= mtimecmp_offset &&
        offset < mtimecmp_offset + sizeof(mtimecmp_)) {
        return write_register(
            mtimecmp_,
            sizeof(mtimecmp_),
            offset - mtimecmp_offset,
            width,
            value);
    }

    if (offset >= mtime_offset &&
        offset < mtime_offset + sizeof(mtime_)) {
        return write_register(
            mtime_,
            sizeof(mtime_),
            offset - mtime_offset,
            width,
            value);
    }

    return BusFault::None;
}

void Clint::tick(
    platform::DmaAccess& dma,
    std::uint64_t cycles)
{
    static_cast<void>(dma);
    mtime_ += cycles;
}

std::uint64_t Clint::time_value() const noexcept
{
    return mtime_;
}

bool Clint::machine_software_irq() const noexcept
{
    return (msip_ & 0x1U) != 0;
}

bool Clint::machine_timer_irq() const noexcept
{
    return mtime_ >= mtimecmp_;
}

std::uint64_t Clint::mtime() const noexcept
{
    return mtime_;
}

std::uint64_t Clint::mtimecmp() const noexcept
{
    return mtimecmp_;
}

std::uint32_t Clint::msip() const noexcept
{
    return msip_;
}

ReadResult Clint::read_register(
    std::uint64_t register_value,
    std::uint64_t register_size,
    std::uint64_t byte_offset,
    AccessWidth width) noexcept
{
    const auto count = static_cast<std::uint64_t>(width_bytes(width));
    if (count > register_size ||
        byte_offset > register_size - count) {
        return {.fault = BusFault::OutOfRange};
    }

    const auto bit_count = static_cast<unsigned int>(count * 8U);
    const auto mask =
        bit_count == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t{1} << bit_count) - 1U;
    const auto shift = static_cast<unsigned int>(byte_offset * 8U);
    return {
        .fault = BusFault::None,
        .value = (register_value >> shift) & mask,
    };
}

BusFault Clint::write_register(
    std::uint64_t& register_value,
    std::uint64_t register_size,
    std::uint64_t byte_offset,
    AccessWidth width,
    std::uint64_t value) noexcept
{
    const auto count = static_cast<std::uint64_t>(width_bytes(width));
    if (count > register_size ||
        byte_offset > register_size - count) {
        return BusFault::OutOfRange;
    }

    const auto bit_count = static_cast<unsigned int>(count * 8U);
    const auto value_mask =
        bit_count == 64U
            ? std::numeric_limits<std::uint64_t>::max()
            : (std::uint64_t{1} << bit_count) - 1U;
    const auto shift = static_cast<unsigned int>(byte_offset * 8U);
    const auto positioned_mask = value_mask << shift;
    register_value =
        (register_value & ~positioned_mask) |
        ((value & value_mask) << shift);
    return BusFault::None;
}

} // namespace rv::devices
