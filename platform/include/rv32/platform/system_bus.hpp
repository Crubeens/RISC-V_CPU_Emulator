#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "rv32/core/bus.hpp"
#include "rv32/platform/device.hpp"

namespace rv32::platform {

struct DeviceInfo {
    std::string name;
    AddressRange range;
};

class SystemBus final : public CpuBus, public DmaAccess {
  public:
    SystemBus() = default;
    ~SystemBus() override = default;

    SystemBus(const SystemBus&) = delete;
    SystemBus& operator=(const SystemBus&) = delete;
    SystemBus(SystemBus&&) = delete;
    SystemBus& operator=(SystemBus&&) = delete;

    template <typename DeviceType, typename... Arguments>
    DeviceType& emplace_device(Arguments&&... arguments)
    {
        auto device = std::make_unique<DeviceType>(
            std::forward<Arguments>(arguments)...);
        auto& reference = *device;
        add_device(std::move(device));
        return reference;
    }

    void add_device(std::unique_ptr<Device> device);
    void set_time_source(const TimeSource* time_source) noexcept;

    [[nodiscard]] std::vector<DeviceInfo> device_map() const;

    void tick_devices(std::uint64_t cycles);

    [[nodiscard]] ReadResult read(
        PhysAddr address,
        AccessWidth width,
        AccessKind kind) override;

    [[nodiscard]] BusFault write(
        PhysAddr address,
        AccessWidth width,
        std::uint64_t value,
        AccessKind kind) override;

    [[nodiscard]] ReadResult load_reserved_word(
        std::uint32_t hart_id,
        PhysAddr address) override;

    [[nodiscard]] StoreConditionalResult store_conditional_word(
        std::uint32_t hart_id,
        PhysAddr address,
        std::uint32_t value) override;

    [[nodiscard]] AtomicResult atomic_word(
        std::uint32_t hart_id,
        PhysAddr address,
        AmoOperation operation,
        std::uint32_t operand) override;

    [[nodiscard]] std::uint64_t read_time() const noexcept override;

    [[nodiscard]] ReadResult dma_read(
        PhysAddr address,
        AccessWidth width) override;

    [[nodiscard]] BusFault dma_write(
        PhysAddr address,
        AccessWidth width,
        std::uint64_t value) override;

  private:
    struct Reservation {
        PhysAddr address{};
        std::uint64_t write_epoch{};
    };

    [[nodiscard]] Device* find_device(
        PhysAddr address,
        AccessWidth width) const noexcept;

    std::vector<std::unique_ptr<Device>> devices_;
    std::unordered_map<std::uint32_t, Reservation> reservations_;
    const TimeSource* time_source_{};
    std::uint64_t write_epoch_{};
};

} // namespace rv32::platform
