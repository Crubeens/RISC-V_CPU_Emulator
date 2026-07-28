#include "rv32/devices/framebuffer.hpp"

#include <limits>
#include <stdexcept>

#include "rv32/platform/endian.hpp"

namespace rv32::devices {

Framebuffer::Framebuffer(
    PhysAddr base,
    std::uint32_t width,
    std::uint32_t height,
    std::uint32_t bytes_per_pixel)
    : width_(width),
      height_(height),
      bytes_per_pixel_(bytes_per_pixel)
{
    if (width == 0 || height == 0 || bytes_per_pixel == 0) {
        throw std::invalid_argument(
            "framebuffer dimensions must be non-zero");
    }

    const auto pixel_count =
        static_cast<std::uint64_t>(width) *
        static_cast<std::uint64_t>(height);
    if (pixel_count >
        std::numeric_limits<std::uint64_t>::max() /
            static_cast<std::uint64_t>(bytes_per_pixel)) {
        throw std::invalid_argument("framebuffer is too large");
    }
    const auto byte_count =
        pixel_count * static_cast<std::uint64_t>(bytes_per_pixel);

    if (byte_count >
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument("framebuffer is too large");
    }

    range_ = {.base = base, .size = byte_count};
    pixels_.resize(static_cast<std::size_t>(byte_count));
}

std::string_view Framebuffer::name() const noexcept
{
    return "Linear Framebuffer";
}

platform::AddressRange Framebuffer::range() const noexcept
{
    return range_;
}

ReadResult Framebuffer::read(
    std::uint64_t offset,
    AccessWidth width)
{
    return platform::read_little_endian(pixels_, offset, width);
}

BusFault Framebuffer::write(
    std::uint64_t offset,
    AccessWidth width,
    std::uint64_t value)
{
    const auto fault =
        platform::write_little_endian(pixels_, offset, width, value);
    if (fault == BusFault::None) {
        dirty_ = true;
    }
    return fault;
}

std::uint32_t Framebuffer::width() const noexcept
{
    return width_;
}

std::uint32_t Framebuffer::height() const noexcept
{
    return height_;
}

std::uint32_t Framebuffer::bytes_per_pixel() const noexcept
{
    return bytes_per_pixel_;
}

bool Framebuffer::dirty() const noexcept
{
    return dirty_;
}

void Framebuffer::clear_dirty() noexcept
{
    dirty_ = false;
}

std::span<const std::uint8_t> Framebuffer::pixels() const noexcept
{
    return pixels_;
}

} // namespace rv32::devices
