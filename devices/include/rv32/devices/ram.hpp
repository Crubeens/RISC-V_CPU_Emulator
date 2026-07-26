#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "rv32/platform/device.hpp"

namespace rv32::devices {

class Ram final : public platform::Device {
  public:
    Ram(PhysAddr base, std::uint64_t size);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] platform::AddressRange range() const noexcept override;

    [[nodiscard]] ReadResult read(
        std::uint64_t offset,
        AccessWidth width) override;

    [[nodiscard]] BusFault write(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t value) override;

    [[nodiscard]] BusFault load_image(
        std::span<const std::uint8_t> image,
        std::uint64_t offset = 0) noexcept;

    [[nodiscard]] std::span<std::uint8_t> bytes() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept;

  private:
    platform::AddressRange range_;
    std::vector<std::uint8_t> data_;
};

} // namespace rv32::devices
