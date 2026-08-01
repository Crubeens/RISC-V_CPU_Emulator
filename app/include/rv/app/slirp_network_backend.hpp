#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "rv/devices/network_backend.hpp"

namespace rv::app {

struct SlirpNetworkStatistics {
    std::uint64_t guest_to_host_frames{};
    std::uint64_t guest_to_host_bytes{};
    std::uint64_t host_to_guest_frames{};
    std::uint64_t host_to_guest_bytes{};
    std::uint64_t dropped_host_frames{};
    std::uint64_t queued_host_frames{};
    std::uint64_t peak_queued_host_frames{};
    std::uint64_t poll_calls{};
    std::uint64_t poll_socket_observations{};
    std::uint64_t poll_ready_events{};
    std::uint64_t poll_errors{};
    std::int32_t last_poll_error{};
    std::uint64_t poll_socket_registrations{};
    std::uint64_t poll_socket_unregistrations{};
    std::uint64_t guest_errors{};
};

class SlirpNetworkBackend final : public devices::NetworkBackend {
  public:
    SlirpNetworkBackend();
    ~SlirpNetworkBackend() override;

    SlirpNetworkBackend(const SlirpNetworkBackend&) = delete;
    SlirpNetworkBackend& operator=(const SlirpNetworkBackend&) = delete;
    SlirpNetworkBackend(SlirpNetworkBackend&&) = delete;
    SlirpNetworkBackend& operator=(SlirpNetworkBackend&&) = delete;

    [[nodiscard]] std::uint64_t tick_interval_cycles()
        const noexcept override;
    void tick(std::uint64_t cycles) override;
    void send_frame(
        std::span<const std::uint8_t> frame) override;
    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    receive_frame() override;

    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::string_view error() const noexcept;
    [[nodiscard]] std::string_view version() const noexcept;
    [[nodiscard]] const SlirpNetworkStatistics& statistics()
        const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rv::app
