#pragma once

#include <cstdint>
#include <string_view>

#include "rv/platform/device.hpp"

namespace rv::devices {

class Clint final : public platform::Device, public platform::TimeSource {
  public:
    Clint(PhysAddr base, std::uint64_t size);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] platform::AddressRange range() const noexcept override;

    [[nodiscard]] ReadResult read(
        std::uint64_t offset,
        AccessWidth width) override;

    [[nodiscard]] BusFault write(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t value) override;

    [[nodiscard]] bool needs_tick() const noexcept override
    {
        return true;
    }

    void tick(
        platform::DmaAccess& dma,
        std::uint64_t cycles) override;

    [[nodiscard]] std::uint64_t time_value() const noexcept override;
    [[nodiscard]] bool machine_software_irq() const noexcept;
    [[nodiscard]] bool machine_timer_irq() const noexcept;

    [[nodiscard]] std::uint64_t mtime() const noexcept;
    [[nodiscard]] std::uint64_t mtimecmp() const noexcept;
    [[nodiscard]] std::uint32_t msip() const noexcept;

  private:
    static constexpr std::uint64_t msip_offset = 0x0000;
    static constexpr std::uint64_t mtimecmp_offset = 0x4000;
    static constexpr std::uint64_t mtime_offset = 0xBFF8;

    [[nodiscard]] static ReadResult read_register(
        std::uint64_t register_value,
        std::uint64_t register_size,
        std::uint64_t byte_offset,
        AccessWidth width) noexcept;

    [[nodiscard]] static BusFault write_register(
        std::uint64_t& register_value,
        std::uint64_t register_size,
        std::uint64_t byte_offset,
        AccessWidth width,
        std::uint64_t value) noexcept;

    platform::AddressRange range_;
    std::uint64_t mtime_{};
    std::uint64_t mtimecmp_{~std::uint64_t{0}};
    std::uint32_t msip_{};
};

} // namespace rv::devices
