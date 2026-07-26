#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "rv32/core/core.hpp"
#include "rv32/platform/address_map.hpp"
#include "rv32/platform/system_bus.hpp"

namespace rv32::devices {
class Clint;
class Framebuffer;
class Plic;
class Ram;
class Syscon;
class Uart16550;
class VirtioBlock;
} // namespace rv32::devices

namespace rv32::platform {

struct MachineConfig {
    std::uint64_t ram_size{address_map::default_dram_size};
    std::uint64_t virtual_disk_size{16ULL * 1024ULL * 1024ULL};
    std::uint32_t framebuffer_width{640};
    std::uint32_t framebuffer_height{480};
    std::uint32_t framebuffer_bytes_per_pixel{4};
    bool enable_framebuffer{true};
};

class Machine {
  public:
    explicit Machine(const MachineConfig& config = {});
    ~Machine();

    Machine(const Machine&) = delete;
    Machine& operator=(const Machine&) = delete;
    Machine(Machine&&) = delete;
    Machine& operator=(Machine&&) = delete;

    void reset(const ResetConfig& config = {});
    [[nodiscard]] StepResult step(std::uint64_t elapsed_cycles = 1);

    [[nodiscard]] Core& core() noexcept;
    [[nodiscard]] const Core& core() const noexcept;
    [[nodiscard]] SystemBus& bus() noexcept;
    [[nodiscard]] const SystemBus& bus() const noexcept;

    [[nodiscard]] devices::Ram& ram() noexcept;
    [[nodiscard]] devices::Clint& clint() noexcept;
    [[nodiscard]] devices::Plic& plic() noexcept;
    [[nodiscard]] devices::Uart16550& uart() noexcept;
    [[nodiscard]] devices::VirtioBlock& virtio_block() noexcept;
    [[nodiscard]] devices::Syscon& syscon() noexcept;
    [[nodiscard]] devices::Framebuffer* framebuffer() noexcept;

    [[nodiscard]] const IrqLines& irq_lines() const noexcept;
    [[nodiscard]] std::vector<DeviceInfo> device_map() const;

  private:
    SystemBus bus_;
    Core core_;

    devices::Ram* ram_{};
    devices::Clint* clint_{};
    devices::Plic* plic_{};
    devices::Uart16550* uart_{};
    devices::VirtioBlock* virtio_block_{};
    devices::Syscon* syscon_{};
    devices::Framebuffer* framebuffer_{};

    IrqLines irq_lines_{};
};

} // namespace rv32::platform
