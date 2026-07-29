#pragma once

#include <cstdint>
#include <string_view>

#include "rv/platform/device.hpp"

namespace rv::devices {

class GoldfishRtc final : public platform::Device {
  public:
    GoldfishRtc(PhysAddr base, std::uint64_t size);

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

    [[nodiscard]] bool interrupt_pending() const noexcept;
    [[nodiscard]] std::uint64_t time_nanoseconds() const noexcept;

  private:
    static constexpr std::uint64_t time_low_offset = 0x00;
    static constexpr std::uint64_t time_high_offset = 0x04;
    static constexpr std::uint64_t alarm_low_offset = 0x08;
    static constexpr std::uint64_t alarm_high_offset = 0x0C;
    static constexpr std::uint64_t irq_enabled_offset = 0x10;
    static constexpr std::uint64_t clear_alarm_offset = 0x14;
    static constexpr std::uint64_t alarm_status_offset = 0x18;
    static constexpr std::uint64_t clear_interrupt_offset = 0x1C;

    void set_time(std::uint64_t nanoseconds) noexcept;

    platform::AddressRange range_;
    std::int64_t host_time_offset_ns_{};
    std::uint32_t latched_time_high_{};
    std::uint32_t pending_time_high_{};
    std::uint32_t pending_alarm_high_{};
    std::uint64_t alarm_time_ns_{};
    bool alarm_active_{};
    bool irq_enabled_{};
    bool interrupt_pending_{};
};

} // namespace rv::devices
