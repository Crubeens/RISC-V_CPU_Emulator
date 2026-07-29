#pragma once

#include <cstdint>
#include <span>

#include "rv64/core/core.hpp"
#include "rv64/platform/address_map.hpp"
#include "rv64/platform/boot.hpp"
#include "rv/platform/system_bus.hpp"

namespace rv::devices {
class Clint;
class Framebuffer;
class Plic;
class Ram;
class Syscon;
class Uart16550;
class VirtioBlock;
} // namespace rv::devices

namespace rv64::platform {

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
    [[nodiscard]] rv::BusFault load_image(
        std::span<const std::uint8_t> image,
        rv::PhysAddr physical_address) noexcept;
    [[nodiscard]] BootResult load_boot(
        const BootConfig& config) noexcept;
    [[nodiscard]] StepResult step(std::uint64_t elapsed_cycles = 1);

    [[nodiscard]] Core& core() noexcept;
    [[nodiscard]] const Core& core() const noexcept;
    [[nodiscard]] rv::platform::SystemBus& bus() noexcept;
    [[nodiscard]] const rv::platform::SystemBus& bus() const noexcept;

    [[nodiscard]] rv::devices::Ram& ram() noexcept;
    [[nodiscard]] rv::devices::Clint& clint() noexcept;
    [[nodiscard]] rv::devices::Plic& plic() noexcept;
    [[nodiscard]] rv::devices::Uart16550& uart() noexcept;
    [[nodiscard]] rv::devices::VirtioBlock& virtio_block() noexcept;
    [[nodiscard]] rv::devices::Syscon& syscon() noexcept;
    [[nodiscard]] rv::devices::Framebuffer* framebuffer() noexcept;
    [[nodiscard]] const IrqLines& irq_lines() const noexcept;

  private:
    rv::platform::SystemBus bus_;
    Core core_;

    rv::devices::Ram* ram_{};
    rv::devices::Clint* clint_{};
    rv::devices::Plic* plic_{};
    rv::devices::Uart16550* uart_{};
    rv::devices::VirtioBlock* virtio_block_{};
    rv::devices::Syscon* syscon_{};
    rv::devices::Framebuffer* framebuffer_{};
    IrqLines irq_lines_{};
};

} // namespace rv64::platform
