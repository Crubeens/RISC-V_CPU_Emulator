#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

#include "rv32/platform/device.hpp"

namespace rv32::devices {

class Uart16550 final : public platform::Device {
  public:
    Uart16550(PhysAddr base, std::uint64_t size);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] platform::AddressRange range() const noexcept override;

    [[nodiscard]] ReadResult read(
        std::uint64_t offset,
        AccessWidth width) override;

    [[nodiscard]] BusFault write(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t value) override;

    void inject_received(std::string_view text);
    [[nodiscard]] std::string take_transmitted();
    [[nodiscard]] bool interrupt_pending() const noexcept;

  private:
    [[nodiscard]] bool divisor_latch_access() const noexcept;
    [[nodiscard]] std::uint8_t line_status() const noexcept;
    [[nodiscard]] std::uint8_t interrupt_identification();

    platform::AddressRange range_;
    std::deque<std::uint8_t> receive_fifo_;
    std::string transmit_buffer_;

    std::uint8_t interrupt_enable_{};
    std::uint8_t fifo_control_{};
    std::uint8_t line_control_{};
    std::uint8_t modem_control_{};
    std::uint8_t modem_status_{};
    std::uint8_t scratch_{};
    std::uint8_t divisor_low_{1};
    std::uint8_t divisor_high_{};
    bool transmit_interrupt_pending_{};
};

} // namespace rv32::devices
