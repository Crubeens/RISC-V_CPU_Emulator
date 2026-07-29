#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rv::devices {

class NetworkBackend {
  public:
    virtual ~NetworkBackend() = default;

    virtual void tick(std::uint64_t cycles)
    {
        static_cast<void>(cycles);
    }

    virtual void send_frame(
        std::span<const std::uint8_t> frame) = 0;

    [[nodiscard]] virtual std::optional<std::vector<std::uint8_t>>
    receive_frame() = 0;
};

} // namespace rv::devices
