#include "rv/devices/framebuffer.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "rv/platform/endian.hpp"

namespace rv::devices {

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
        const std::uint64_t byte_count = width_bytes(width);
        const std::uint64_t first_pixel =
            offset / bytes_per_pixel_;
        const std::uint64_t last_pixel =
            (offset + byte_count - 1U) / bytes_per_pixel_;
        const auto first_y = static_cast<std::uint32_t>(
            first_pixel / width_);
        const auto last_y = static_cast<std::uint32_t>(
            last_pixel / width_);
        FramebufferDirtyRegion update;
        update.y = first_y;
        update.height = last_y - first_y + 1U;
        if (first_y == last_y) {
            update.x = static_cast<std::uint32_t>(
                first_pixel % width_);
            const auto last_x = static_cast<std::uint32_t>(
                last_pixel % width_);
            update.width = last_x - update.x + 1U;
        } else {
            // A single write crossing scanlines is rare. Conservatively mark
            // every touched scanline to keep the region rectangular.
            update.x = 0U;
            update.width = width_;
        }

        if (dirty_region_.empty()) {
            dirty_region_ = update;
        } else {
            const std::uint32_t left =
                std::min(dirty_region_.x, update.x);
            const std::uint32_t top =
                std::min(dirty_region_.y, update.y);
            const std::uint32_t right =
                std::max(
                    dirty_region_.x + dirty_region_.width,
                    update.x + update.width);
            const std::uint32_t bottom =
                std::max(
                    dirty_region_.y + dirty_region_.height,
                    update.y + update.height);
            dirty_region_ = {
                .x = left,
                .y = top,
                .width = right - left,
                .height = bottom - top,
            };
        }
        ++statistics_.write_operations;
        statistics_.bytes_written += byte_count;
        ++statistics_.dirty_region_updates;
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
    return !dirty_region_.empty();
}

FramebufferDirtyRegion Framebuffer::dirty_region() const noexcept
{
    return dirty_region_;
}

void Framebuffer::clear_dirty() noexcept
{
    dirty_region_ = {};
}

std::span<const std::uint8_t> Framebuffer::pixels() const noexcept
{
    return pixels_;
}

const FramebufferStatistics&
Framebuffer::statistics() const noexcept
{
    return statistics_;
}

} // namespace rv::devices
