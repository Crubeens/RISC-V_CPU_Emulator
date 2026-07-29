#include "rv/devices/ram.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "rv/platform/endian.hpp"

namespace rv::devices {

Ram::Ram(PhysAddr base, std::uint64_t size)
    : range_{.base = base, .size = size}
{
    if (size == 0 ||
        size > static_cast<std::uint64_t>(
                   std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("RAM size is invalid");
    }
    data_.resize(static_cast<std::size_t>(size));
}

std::string_view Ram::name() const noexcept
{
    return "RAM";
}

platform::AddressRange Ram::range() const noexcept
{
    return range_;
}

ReadResult Ram::read(
    std::uint64_t offset,
    AccessWidth width)
{
    return platform::read_little_endian(data_, offset, width);
}

BusFault Ram::write(
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value)
{
    return platform::write_little_endian(data_, offset, width, value);
}

BusFault Ram::load_image(
    std::span<const std::uint8_t> image,
    std::uint64_t offset) noexcept
{
    const auto available = static_cast<std::uint64_t>(data_.size());
    const auto count = static_cast<std::uint64_t>(image.size());
    if (count > available || offset > available - count) {
        return BusFault::OutOfRange;
    }

    std::copy(
        image.begin(),
        image.end(),
        data_.begin() + static_cast<std::ptrdiff_t>(offset));
    return BusFault::None;
}

std::span<std::uint8_t> Ram::bytes() noexcept
{
    return data_;
}

std::span<const std::uint8_t> Ram::bytes() const noexcept
{
    return data_;
}

} // namespace rv::devices
