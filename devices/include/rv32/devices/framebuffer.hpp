#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "rv32/platform/device.hpp"

namespace rv32::devices {

class Framebuffer final : public platform::Device {
  public:
    Framebuffer(
        PhysAddr base,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t bytes_per_pixel);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] platform::AddressRange range() const noexcept override;

    [[nodiscard]] ReadResult read(
        std::uint64_t offset,
        AccessWidth width) override;

    [[nodiscard]] BusFault write(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t value) override;

    [[nodiscard]] std::uint32_t width() const noexcept;
    [[nodiscard]] std::uint32_t height() const noexcept;
    [[nodiscard]] std::uint32_t bytes_per_pixel() const noexcept;
    [[nodiscard]] bool dirty() const noexcept;
    void clear_dirty() noexcept;

    [[nodiscard]] std::span<const std::uint8_t> pixels() const noexcept;

  private:
    platform::AddressRange range_;
    std::uint32_t width_{};
    std::uint32_t height_{};
    std::uint32_t bytes_per_pixel_{};
    std::vector<std::uint8_t> pixels_;
    bool dirty_{};
};

} // namespace rv32::devices
