#pragma once

#include <cstdint>
#include <string_view>

#include "rv/platform/device.hpp"

namespace rv::devices {

enum class SystemAction : std::uint8_t {
    None,
    PowerOff,
    Reboot,
};

class Syscon final : public platform::Device {
  public:
    static constexpr std::uint32_t poweroff_value = 0x5555;
    static constexpr std::uint32_t reboot_value = 0x7777;

    Syscon(PhysAddr base, std::uint64_t size);

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] platform::AddressRange range() const noexcept override;

    [[nodiscard]] ReadResult read(
        std::uint64_t offset,
        AccessWidth width) override;

    [[nodiscard]] BusFault write(
        std::uint64_t offset,
        AccessWidth width,
        std::uint64_t value) override;

    [[nodiscard]] SystemAction requested_action() const noexcept;
    void clear_action() noexcept;

  private:
    platform::AddressRange range_;
    SystemAction action_{SystemAction::None};
};

} // namespace rv::devices
