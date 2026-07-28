#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "rv32/platform/device.hpp"

namespace rv32::devices {

struct VirtioBlockStatistics {
    std::uint64_t queue_notify_writes{};
    std::uint64_t queue_notifications{};
    std::uint64_t rejected_notifications{};
    std::uint64_t descriptor_chains{};
    std::uint64_t completed_requests{};
    std::uint64_t read_requests{};
    std::uint64_t write_requests{};
    std::uint64_t failed_requests{};
    std::uint64_t bytes_transferred{};
    std::uint64_t interrupts_raised{};
    std::uint64_t interrupt_acknowledgements{};
};

struct VirtioBlockQueueState {
    std::uint32_t page_size{};
    std::uint32_t selected_queue{};
    std::uint16_t queue_size{};
    std::uint32_t alignment{};
    std::uint32_t page_frame_number{};
    std::uint8_t device_status{};
    bool configured{};
};

class VirtioBlock final : public platform::Device {
  public:
    static constexpr std::uint32_t sector_size = 512;
    static constexpr std::uint16_t maximum_queue_size = 128;

    VirtioBlock(
        PhysAddr base,
        std::uint64_t size,
        std::vector<std::uint8_t> disk_image);

    VirtioBlock(
        PhysAddr base,
        std::uint64_t size,
        std::uint64_t disk_size);

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
    [[nodiscard]] bool dirty() const noexcept;
    void clear_dirty() noexcept;
    [[nodiscard]] const VirtioBlockStatistics& statistics()
        const noexcept;
    [[nodiscard]] VirtioBlockQueueState queue_state()
        const noexcept;

    [[nodiscard]] std::span<std::uint8_t> disk_image() noexcept;
    [[nodiscard]] std::span<const std::uint8_t> disk_image() const noexcept;

  private:
    struct Descriptor {
        std::uint64_t address{};
        std::uint32_t length{};
        std::uint16_t flags{};
        std::uint16_t next{};
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
    static constexpr std::uint32_t block_device_id = 2;
    static constexpr std::uint32_t vendor_id = 0x554D4551;

    static constexpr std::uint16_t descriptor_has_next = 0x1;
    static constexpr std::uint16_t descriptor_is_write = 0x2;

    static constexpr std::uint32_t request_read = 0;
    static constexpr std::uint32_t request_write = 1;
    static constexpr std::uint8_t status_ok = 0;
    static constexpr std::uint8_t status_io_error = 1;
    static constexpr std::uint8_t status_unsupported = 2;

    void reset() noexcept;
    void update_queue_addresses() noexcept;
    void process_queue(platform::DmaAccess& dma);

    [[nodiscard]] bool process_request(
        platform::DmaAccess& dma,
        std::uint16_t head,
        std::uint32_t& bytes_written);

    [[nodiscard]] bool load_descriptor(
        platform::DmaAccess& dma,
        std::uint16_t index,
        Descriptor& descriptor);

    [[nodiscard]] static ReadResult dma_read_value(
        platform::DmaAccess& dma,
        PhysAddr address,
        AccessWidth width);

    [[nodiscard]] static bool dma_write_value(
        platform::DmaAccess& dma,
        PhysAddr address,
        AccessWidth width,
        std::uint64_t value);

    [[nodiscard]] bool queue_configured() const noexcept;

    platform::AddressRange range_;
    std::vector<std::uint8_t> disk_;

    std::uint32_t device_features_select_{};
    std::uint32_t driver_features_select_{};
    std::uint32_t driver_features_{};
    std::uint32_t guest_page_size_{};
    std::uint32_t queue_select_{};
    std::uint16_t queue_num_{};
    std::uint32_t queue_align_{4096};
    std::uint32_t queue_pfn_{};
    std::uint8_t interrupt_status_{};
    std::uint8_t device_status_{};

    PhysAddr descriptor_table_{};
    PhysAddr available_ring_{};
    PhysAddr used_ring_{};
    std::uint16_t last_available_index_{};
    std::uint16_t used_index_{};

    bool notification_pending_{};
    bool disk_dirty_{};
    VirtioBlockStatistics statistics_{};
};

} // namespace rv32::devices
