#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <span>
#include <string_view>
#include <vector>

#include "rv/devices/network_backend.hpp"
#include "rv/platform/device.hpp"

namespace rv::devices {

struct VirtioNetStatistics {
    std::uint64_t queue_notify_writes{};
    std::uint64_t accepted_notifications{};
    std::uint64_t rejected_notifications{};
    std::uint64_t descriptor_chains{};
    std::uint64_t transmitted_frames{};
    std::uint64_t transmitted_bytes{};
    std::uint64_t received_frames{};
    std::uint64_t received_bytes{};
    std::uint64_t dropped_transmit_frames{};
    std::uint64_t dropped_receive_frames{};
    std::uint64_t dma_failures{};
    std::uint64_t interrupts_raised{};
    std::uint64_t interrupt_acknowledgements{};
};

struct VirtioNetQueueState {
    std::uint16_t queue_size{};
    std::uint32_t alignment{};
    std::uint32_t page_frame_number{};
    std::uint16_t last_available_index{};
    std::uint16_t used_index{};
    bool configured{};
};

class VirtioNet final : public platform::Device {
  public:
    static constexpr std::uint16_t maximum_queue_size = 256;
    static constexpr std::size_t maximum_frame_size = 65550;
    static constexpr std::size_t maximum_pending_frames = 256;
    static constexpr std::size_t net_header_size = 10;
    static constexpr std::uint32_t receive_queue = 0;
    static constexpr std::uint32_t transmit_queue = 1;

    using MacAddress = std::array<std::uint8_t, 6>;

    VirtioNet(
        PhysAddr base,
        std::uint64_t size,
        MacAddress mac = {0x02U, 0x52U, 0x56U, 0x00U, 0x00U, 0x01U});

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

    void set_backend(NetworkBackend* backend) noexcept;
    [[nodiscard]] NetworkBackend* backend() const noexcept;
    [[nodiscard]] bool inject_received_frame(
        std::span<const std::uint8_t> frame);

    [[nodiscard]] bool interrupt_pending() const noexcept;
    [[nodiscard]] MacAddress mac_address() const noexcept;
    [[nodiscard]] std::uint8_t device_status() const noexcept;
    [[nodiscard]] std::uint32_t selected_queue() const noexcept;
    [[nodiscard]] VirtioNetQueueState queue_state(
        std::uint32_t queue) const noexcept;
    [[nodiscard]] const VirtioNetStatistics& statistics()
        const noexcept;

  private:
    struct Descriptor {
        std::uint64_t address{};
        std::uint32_t length{};
        std::uint16_t flags{};
        std::uint16_t next{};
    };

    struct Queue {
        std::uint16_t size{};
        std::uint32_t alignment{4096};
        std::uint32_t page_frame_number{};
        PhysAddr descriptor_table{};
        PhysAddr available_ring{};
        PhysAddr used_ring{};
        std::uint16_t last_available_index{};
        std::uint16_t used_index{};
        bool notification_pending{};
    };

    static constexpr std::uint64_t magic_value_offset = 0x000;
    static constexpr std::uint64_t version_offset = 0x004;
    static constexpr std::uint64_t device_id_offset = 0x008;
    static constexpr std::uint64_t vendor_id_offset = 0x00C;
    static constexpr std::uint64_t device_features_offset = 0x010;
    static constexpr std::uint64_t device_features_select_offset = 0x014;
    static constexpr std::uint64_t driver_features_offset = 0x020;
    static constexpr std::uint64_t driver_features_select_offset = 0x024;
    static constexpr std::uint64_t guest_page_size_offset = 0x028;
    static constexpr std::uint64_t queue_select_offset = 0x030;
    static constexpr std::uint64_t queue_num_max_offset = 0x034;
    static constexpr std::uint64_t queue_num_offset = 0x038;
    static constexpr std::uint64_t queue_align_offset = 0x03C;
    static constexpr std::uint64_t queue_pfn_offset = 0x040;
    static constexpr std::uint64_t queue_notify_offset = 0x050;
    static constexpr std::uint64_t interrupt_status_offset = 0x060;
    static constexpr std::uint64_t interrupt_ack_offset = 0x064;
    static constexpr std::uint64_t status_offset = 0x070;
    static constexpr std::uint64_t config_offset = 0x100;

    static constexpr std::uint32_t magic_value = 0x74726976;
    static constexpr std::uint32_t legacy_version = 1;
    static constexpr std::uint32_t network_device_id = 1;
    static constexpr std::uint32_t vendor_id = 0x554D4551;
    static constexpr std::uint32_t feature_mac = 1U << 5U;
    static constexpr std::uint32_t feature_status = 1U << 16U;
    static constexpr std::uint16_t link_up = 1;

    static constexpr std::uint16_t descriptor_has_next = 0x1;
    static constexpr std::uint16_t descriptor_is_write = 0x2;

    void reset() noexcept;
    void update_queue_addresses(Queue& queue) noexcept;
    void collect_backend_frames(std::uint64_t cycles);
    void process_transmit_queue(platform::DmaAccess& dma);
    void process_receive_queue(platform::DmaAccess& dma);

    [[nodiscard]] bool load_chain(
        platform::DmaAccess& dma,
        const Queue& queue,
        std::uint16_t head,
        std::vector<Descriptor>& chain);

    [[nodiscard]] bool read_available_head(
        platform::DmaAccess& dma,
        const Queue& queue,
        std::uint16_t& head);

    [[nodiscard]] bool complete_chain(
        platform::DmaAccess& dma,
        Queue& queue,
        std::uint16_t head,
        std::uint32_t bytes_written);

    [[nodiscard]] bool transmit_chain(
        platform::DmaAccess& dma,
        const std::vector<Descriptor>& chain);

    [[nodiscard]] bool receive_chain(
        platform::DmaAccess& dma,
        const std::vector<Descriptor>& chain,
        std::span<const std::uint8_t> frame,
        std::uint32_t& bytes_written);

    [[nodiscard]] static ReadResult dma_read_value(
        platform::DmaAccess& dma,
        PhysAddr address,
        AccessWidth width);

    [[nodiscard]] static bool dma_write_value(
        platform::DmaAccess& dma,
        PhysAddr address,
        AccessWidth width,
        std::uint64_t value);

    [[nodiscard]] bool queue_configured(
        const Queue& queue) const noexcept;
    [[nodiscard]] Queue* selected_queue_entry() noexcept;
    [[nodiscard]] const Queue* selected_queue_entry() const noexcept;

    platform::AddressRange range_;
    MacAddress mac_;
    std::array<Queue, 2> queues_{};
    std::deque<std::vector<std::uint8_t>> pending_receive_frames_;
    NetworkBackend* backend_{};

    std::uint32_t device_features_select_{};
    std::uint32_t driver_features_select_{};
    std::uint32_t driver_features_{};
    std::uint32_t guest_page_size_{};
    std::uint32_t queue_select_{};
    std::uint8_t interrupt_status_{};
    std::uint8_t device_status_{};
    VirtioNetStatistics statistics_{};
};

} // namespace rv::devices
